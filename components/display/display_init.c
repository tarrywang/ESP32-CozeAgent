/**
 * @file display_init.c
 * @brief Manual display initialization for ESP32-S3-Touch-AMOLED-1.75
 *
 * Based on example/ESP-IDF-v5.4/05_LVGL_WITH_RAM implementation.
 * Uses QSPI interface with SH8601 driver and LVGL 9.x.
 *
 * CRITICAL: Initialization order matters for memory allocation!
 * Order: SPI bus → Panel IO → Panel init → lv_init() → Buffers → Display create
 */

#include "display_init.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "src/draw/sw/lv_draw_sw.h"  // For lv_draw_sw_rgb565_swap()
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_lcd_sh8601.h"

static const char *TAG = "display_init";

// ============================================
// Private Variables
// ============================================

static esp_lcd_panel_io_handle_t s_panel_io = NULL;
static esp_lcd_panel_handle_t s_panel = NULL;
static lv_display_t *s_lv_disp = NULL;
static SemaphoreHandle_t s_lvgl_mutex = NULL;
static esp_timer_handle_t s_lvgl_tick_timer = NULL;
static TaskHandle_t s_lvgl_task_handle = NULL;

// LVGL draw buffers
static void *s_buf1 = NULL;
static void *s_buf2 = NULL;

// ============================================
// SH8601 Initialization Commands
// ============================================

static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},  // 16-bit RGB565
    {0x35, (uint8_t[]){0x00}, 1, 0},  // Tearing effect line on
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 0},  // Brightness max
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x06, 0x01, 0xD7}, 4, 0},   // Column address
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xD1}, 4, 600}, // Row address
    {0x11, NULL, 0, 600},  // Sleep out
    {0x29, NULL, 0, 0},    // Display on
};

// ============================================
// Private Functions
// ============================================

/**
 * @brief LVGL display flush callback (LVGL 9.x API)
 *
 * IMPORTANT: SH8601 display requires RGB565 byte swapping for correct colors.
 * Without swapping, colors appear inverted (e.g., dark gray shows as magenta).
 *
 * We swap bytes, send to display, then call flush_ready immediately.
 * This makes the operation synchronous to avoid buffer race conditions.
 * The SPI panel_io still uses async transfer internally, but LVGL won't
 * reuse the buffer until we explicitly allow it.
 */
static void lvgl_flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel = lv_display_get_user_data(display);

    int x1 = area->x1;
    int y1 = area->y1;
    int x2 = area->x2;
    int y2 = area->y2;

    // Swap RGB565 bytes for SH8601 display (required for correct color rendering)
    // This matches the BSP's .swap_bytes = true behavior
    uint32_t len = (x2 - x1 + 1) * (y2 - y1 + 1);
    lv_draw_sw_rgb565_swap(px_map, len);

    // Draw bitmap to panel
    esp_lcd_panel_draw_bitmap(panel, x1, y1, x2 + 1, y2 + 1, px_map);

    // Note: flush_ready is called from panel_io_done_cb when SPI transfer completes
    // The buffer should not be modified until then
}

/**
 * @brief SPI transfer done callback - CRITICAL for proper LVGL synchronization
 *
 * This callback notifies LVGL that the SPI transfer is complete,
 * allowing it to safely submit the next frame.
 * Uses global s_lv_disp since callback is registered before display is created.
 */
static bool panel_io_done_cb(esp_lcd_panel_io_handle_t panel_io,
                              esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    // Use global display handle (set after display creation)
    if (s_lv_disp) {
        lv_display_flush_ready(s_lv_disp);
    }
    return false;
}

/**
 * @brief LVGL tick timer callback
 */
static void lvgl_tick_cb(void *arg)
{
    lv_tick_inc(DISPLAY_LVGL_TICK_MS);
}

/**
 * @brief LVGL rendering task
 */
static void lvgl_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL task started");

    uint32_t task_delay_ms = 500;

    while (1) {
        if (display_lock(-1)) {
            task_delay_ms = lv_timer_handler();
            display_unlock();
        }

        // Clamp delay
        if (task_delay_ms > 500) {
            task_delay_ms = 500;
        } else if (task_delay_ms < 1) {
            task_delay_ms = 1;
        }

        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}

// ============================================
// Public Functions
// ============================================

esp_err_t display_init(void)
{
    if (s_panel != NULL) {
        ESP_LOGW(TAG, "Display already initialized");
        return ESP_OK;
    }

    esp_err_t ret;

    ESP_LOGI(TAG, "Initializing display: %dx%d (QSPI)", DISPLAY_H_RES, DISPLAY_V_RES);

    // ========================================
    // Step 1: Create LVGL mutex
    // ========================================
    s_lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    if (s_lvgl_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create LVGL mutex");
        return ESP_ERR_NO_MEM;
    }

    // ========================================
    // Step 2: Configure RST GPIO and hardware reset
    // ========================================
    gpio_config_t rst_gpio_conf = {
        .pin_bit_mask = (1ULL << DISPLAY_LCD_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&rst_gpio_conf);

    // Hardware reset
    gpio_set_level(DISPLAY_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(DISPLAY_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    // ========================================
    // Step 3: Initialize SPI bus FIRST (before LVGL!)
    // This prevents memory fragmentation from LVGL allocations
    // ========================================
    ESP_LOGI(TAG, "Initializing QSPI bus...");
    spi_bus_config_t buscfg = SH8601_PANEL_BUS_QSPI_CONFIG(
        DISPLAY_LCD_PCLK,
        DISPLAY_LCD_DATA0, DISPLAY_LCD_DATA1,
        DISPLAY_LCD_DATA2, DISPLAY_LCD_DATA3,
        DISPLAY_H_RES * DISPLAY_V_RES * sizeof(uint16_t)
    );
    buscfg.flags = SPICOMMON_BUSFLAG_QUAD;

    ret = spi_bus_initialize(DISPLAY_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        goto err_cleanup;
    }

    // ========================================
    // Step 4: Configure SPI panel IO
    // Callback uses global s_lv_disp (NULL-safe)
    // ========================================
    ESP_LOGI(TAG, "Installing panel IO...");
    esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG(
        DISPLAY_LCD_CS,
        panel_io_done_cb,
        NULL  // user_ctx not needed, callback uses global s_lv_disp
    );

    ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)DISPLAY_SPI_HOST,
                                    &io_config, &s_panel_io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create panel IO: %s", esp_err_to_name(ret));
        goto err_cleanup;
    }

    // ========================================
    // Step 5: Create and initialize SH8601 panel
    // ========================================
    ESP_LOGI(TAG, "Installing SH8601 panel driver...");
    sh8601_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags = {
            .use_qspi_interface = 1,
        },
    };

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = DISPLAY_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_config,
    };

    ret = esp_lcd_new_panel_sh8601(s_panel_io, &panel_config, &s_panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create SH8601 panel: %s", esp_err_to_name(ret));
        goto err_cleanup;
    }

    // Initialize panel hardware
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    // ========================================
    // Step 6: Initialize LVGL (after SPI/panel setup!)
    // ========================================
    ESP_LOGI(TAG, "Initializing LVGL...");
    lv_init();

    // ========================================
    // Step 7: Allocate LVGL buffers IMMEDIATELY after lv_init()
    // This is before lv_display_create() to avoid fragmentation
    // ========================================
    size_t buffer_size = DISPLAY_H_RES * DISPLAY_LVGL_BUF_HEIGHT * 2;  // RGB565 = 2 bytes/pixel
    ESP_LOGI(TAG, "Allocating LVGL buffers: %zu bytes each", buffer_size);

    // Try DMA memory first (fastest), fallback to PSRAM if DMA is exhausted
    // ESP32-S3 GDMA supports PSRAM access (CONFIG_SOC_AHB_GDMA_SUPPORT_PSRAM=y)
    s_buf1 = heap_caps_malloc(buffer_size, MALLOC_CAP_DMA);
    if (s_buf1 == NULL) {
        ESP_LOGW(TAG, "DMA memory exhausted, using PSRAM for buf1");
        s_buf1 = heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM);
    }

    s_buf2 = heap_caps_malloc(buffer_size, MALLOC_CAP_DMA);
    if (s_buf2 == NULL) {
        ESP_LOGW(TAG, "DMA memory exhausted, using PSRAM for buf2");
        s_buf2 = heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM);
    }

    if (s_buf1 == NULL || s_buf2 == NULL) {
        ESP_LOGE(TAG, "Failed to allocate LVGL buffers");
        ret = ESP_ERR_NO_MEM;
        goto err_cleanup;
    }

    ESP_LOGI(TAG, "LVGL buffers allocated successfully (buf1=%p, buf2=%p)", s_buf1, s_buf2);

    // ========================================
    // Step 8: Create LVGL display and set buffers
    // ========================================
    s_lv_disp = lv_display_create(DISPLAY_H_RES, DISPLAY_V_RES);
    if (s_lv_disp == NULL) {
        ESP_LOGE(TAG, "Failed to create LVGL display");
        ret = ESP_FAIL;
        goto err_cleanup;
    }

    // Set color format to RGB565 (2 bytes per pixel)
    lv_display_set_color_format(s_lv_disp, LV_COLOR_FORMAT_RGB565);

    // Set LVGL display buffers
    lv_display_set_buffers(s_lv_disp, s_buf1, s_buf2,
                           buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    // Set flush callback and user data
    lv_display_set_flush_cb(s_lv_disp, lvgl_flush_cb);
    lv_display_set_user_data(s_lv_disp, s_panel);

    // ========================================
    // Step 9: Create LVGL tick timer
    // ========================================
    ESP_LOGI(TAG, "Installing LVGL tick timer...");
    const esp_timer_create_args_t timer_args = {
        .callback = lvgl_tick_cb,
        .name = "lvgl_tick",
    };

    ret = esp_timer_create(&timer_args, &s_lvgl_tick_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LVGL tick timer: %s", esp_err_to_name(ret));
        goto err_cleanup;
    }

    ret = esp_timer_start_periodic(s_lvgl_tick_timer, DISPLAY_LVGL_TICK_MS * 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start LVGL tick timer: %s", esp_err_to_name(ret));
        goto err_cleanup;
    }

    ESP_LOGI(TAG, "Display initialized successfully");
    return ESP_OK;

err_cleanup:
    if (s_lvgl_tick_timer) {
        esp_timer_delete(s_lvgl_tick_timer);
        s_lvgl_tick_timer = NULL;
    }
    if (s_lv_disp) {
        lv_display_delete(s_lv_disp);
        s_lv_disp = NULL;
    }
    if (s_panel) {
        esp_lcd_panel_del(s_panel);
        s_panel = NULL;
    }
    if (s_panel_io) {
        esp_lcd_panel_io_del(s_panel_io);
        s_panel_io = NULL;
    }
    if (s_buf1) {
        heap_caps_free(s_buf1);
        s_buf1 = NULL;
    }
    if (s_buf2) {
        heap_caps_free(s_buf2);
        s_buf2 = NULL;
    }
    if (s_lvgl_mutex) {
        vSemaphoreDelete(s_lvgl_mutex);
        s_lvgl_mutex = NULL;
    }
    return ret;
}

esp_err_t display_deinit(void)
{
    if (s_panel == NULL) {
        return ESP_OK;
    }

    if (s_lvgl_task_handle) {
        vTaskDelete(s_lvgl_task_handle);
        s_lvgl_task_handle = NULL;
    }

    if (s_lvgl_tick_timer) {
        esp_timer_stop(s_lvgl_tick_timer);
        esp_timer_delete(s_lvgl_tick_timer);
        s_lvgl_tick_timer = NULL;
    }

    if (s_lv_disp) {
        lv_display_delete(s_lv_disp);
        s_lv_disp = NULL;
    }
    if (s_panel) {
        esp_lcd_panel_del(s_panel);
        s_panel = NULL;
    }
    if (s_panel_io) {
        esp_lcd_panel_io_del(s_panel_io);
        s_panel_io = NULL;
    }
    if (s_buf1) {
        heap_caps_free(s_buf1);
        s_buf1 = NULL;
    }
    if (s_buf2) {
        heap_caps_free(s_buf2);
        s_buf2 = NULL;
    }
    if (s_lvgl_mutex) {
        vSemaphoreDelete(s_lvgl_mutex);
        s_lvgl_mutex = NULL;
    }

    ESP_LOGI(TAG, "Display deinitialized");
    return ESP_OK;
}

lv_display_t *display_get_lv_disp(void)
{
    return s_lv_disp;
}

bool display_lock(int timeout_ms)
{
    if (s_lvgl_mutex == NULL) {
        return false;
    }

    TickType_t ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(s_lvgl_mutex, ticks) == pdTRUE;
}

void display_unlock(void)
{
    if (s_lvgl_mutex != NULL) {
        xSemaphoreGiveRecursive(s_lvgl_mutex);
    }
}

esp_err_t display_start_task(void)
{
    if (s_lvgl_task_handle != NULL) {
        ESP_LOGW(TAG, "LVGL task already running");
        return ESP_OK;
    }

    BaseType_t ret = xTaskCreate(
        lvgl_task,
        "LVGL",
        DISPLAY_LVGL_TASK_STACK,
        NULL,
        DISPLAY_LVGL_TASK_PRIO,
        &s_lvgl_task_handle
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LVGL task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "LVGL task started");
    return ESP_OK;
}

esp_err_t display_power(bool on)
{
    if (s_panel == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_lcd_panel_disp_on_off(s_panel, on);
    ESP_LOGI(TAG, "Display power %s", on ? "on" : "off");
    return ESP_OK;
}
