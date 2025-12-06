/**
 * @file bsp_display.c
 * @brief AMOLED Display driver implementation for SH8601
 *
 * Supports 466x466 AMOLED display with LVGL 9.x integration.
 */

#include "bsp_display.h"
#include "bsp_board.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_heap_caps.h"
#include "lvgl.h"

static const char *TAG = "BSP_DISPLAY";

// ============================================
// LVGL Configuration
// ============================================
#define LVGL_TICK_PERIOD_MS     2
#define LVGL_BUFFER_LINES       50  // Number of lines in draw buffer

// ============================================
// Private Variables
// ============================================

static esp_lcd_panel_io_handle_t s_panel_io = NULL;
static esp_lcd_panel_handle_t s_panel = NULL;
static lv_display_t *s_lv_disp = NULL;
static SemaphoreHandle_t s_lvgl_mutex = NULL;
static esp_timer_handle_t s_lvgl_tick_timer = NULL;
static bool s_display_on = true;
static uint8_t s_brightness = 100;

// LVGL draw buffers (allocated from PSRAM)
static void *s_buf1 = NULL;
static void *s_buf2 = NULL;

// ============================================
// SH8601 Initialization Commands
// ============================================

typedef struct {
    uint8_t cmd;
    uint8_t data[16];
    uint8_t data_len;
    uint8_t delay_ms;
} lcd_init_cmd_t;

// SH8601 AMOLED initialization sequence
static const lcd_init_cmd_t sh8601_init_cmds[] = {
    // Software reset
    {0x01, {0}, 0, 120},
    // Sleep out
    {0x11, {0}, 0, 120},
    // Set pixel format: 16-bit RGB565
    {0x3A, {0x55}, 1, 0},
    // Memory data access control
    {0x36, {0x00}, 1, 0},
    // Column address set
    {0x2A, {0x00, 0x00, 0x01, 0xD1}, 4, 0},  // 0-465
    // Row address set
    {0x2B, {0x00, 0x00, 0x01, 0xD1}, 4, 0},  // 0-465
    // Tearing effect line on
    {0x35, {0x00}, 1, 0},
    // Display on
    {0x29, {0}, 0, 20},
    // End marker
    {0x00, {0}, 0xFF, 0},
};

// ============================================
// Private Functions
// ============================================

/**
 * @brief LVGL display flush callback (LVGL 9.x API)
 */
static void lvgl_flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel = lv_display_get_user_data(display);

    int x1 = area->x1;
    int y1 = area->y1;
    int x2 = area->x2;
    int y2 = area->y2;

    // Draw bitmap to panel
    esp_lcd_panel_draw_bitmap(panel, x1, y1, x2 + 1, y2 + 1, px_map);
}

/**
 * @brief Panel IO done callback
 */
static bool panel_io_done_cb(esp_lcd_panel_io_handle_t panel_io,
                              esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_display_t *display = (lv_display_t *)user_ctx;
    lv_display_flush_ready(display);
    return false;
}

/**
 * @brief LVGL tick timer callback
 */
static void lvgl_tick_cb(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

/**
 * @brief Initialize backlight PWM
 */
static esp_err_t init_backlight(void)
{
    // Configure LEDC for backlight control
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer_conf);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Backlight timer config failed, may not have hardware backlight");
        return ret;
    }

    ledc_channel_config_t channel_conf = {
        .gpio_num = BSP_LCD_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 255,  // Full brightness
        .hpoint = 0,
    };
    ret = ledc_channel_config(&channel_conf);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Backlight channel config failed");
    }

    return ret;
}

/**
 * @brief Send initialization commands to display
 */
static esp_err_t send_init_commands(void)
{
    const lcd_init_cmd_t *cmd = sh8601_init_cmds;

    while (cmd->data_len != 0xFF) {
        esp_lcd_panel_io_tx_param(s_panel_io, cmd->cmd, cmd->data, cmd->data_len);
        if (cmd->delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(cmd->delay_ms));
        }
        cmd++;
    }

    return ESP_OK;
}

// ============================================
// Public Functions
// ============================================

esp_err_t bsp_display_init(const bsp_display_config_t *config)
{
    if (s_panel != NULL) {
        ESP_LOGW(TAG, "Display already initialized");
        return ESP_OK;
    }

    // Use default config if not provided
    bsp_display_config_t cfg;
    if (config == NULL) {
        cfg = (bsp_display_config_t)BSP_DISPLAY_DEFAULT_CONFIG();
    } else {
        cfg = *config;
    }

    s_brightness = cfg.brightness;

    ESP_LOGI(TAG, "Initializing display: %dx%d, rotation=%d",
             cfg.width, cfg.height, cfg.rotation);

    esp_err_t ret;

    // Create LVGL mutex
    s_lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    if (s_lvgl_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create LVGL mutex");
        return ESP_ERR_NO_MEM;
    }

    // Initialize backlight (optional, may not be present)
    init_backlight();

    // Configure DC (Data/Command) GPIO
    gpio_config_t dc_gpio_conf = {
        .pin_bit_mask = (1ULL << BSP_LCD_DC),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&dc_gpio_conf);

    // Configure RST GPIO
    gpio_config_t rst_gpio_conf = {
        .pin_bit_mask = (1ULL << BSP_LCD_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&rst_gpio_conf);

    // Hardware reset
    gpio_set_level(BSP_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(BSP_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    // Initialize LVGL first (needed before creating display)
    ESP_LOGI(TAG, "Initializing LVGL...");
    lv_init();

    // Create LVGL display (LVGL 9.x API)
    s_lv_disp = lv_display_create(cfg.width, cfg.height);
    if (s_lv_disp == NULL) {
        ESP_LOGE(TAG, "Failed to create LVGL display");
        ret = ESP_FAIL;
        goto err_cleanup;
    }

    // Allocate draw buffers from PSRAM
    size_t buffer_size = cfg.width * LVGL_BUFFER_LINES * sizeof(lv_color_t);
    s_buf1 = heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_buf2 = heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (s_buf1 == NULL || s_buf2 == NULL) {
        ESP_LOGW(TAG, "Failed to allocate LVGL buffers from PSRAM, trying internal RAM");
        if (s_buf1) heap_caps_free(s_buf1);
        if (s_buf2) heap_caps_free(s_buf2);
        s_buf1 = heap_caps_malloc(buffer_size, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        s_buf2 = NULL;  // Single buffer mode
        if (s_buf1 == NULL) {
            ESP_LOGE(TAG, "Failed to allocate LVGL buffers");
            ret = ESP_ERR_NO_MEM;
            goto err_cleanup;
        }
    }

    // Set LVGL display buffers (LVGL 9.x API)
    lv_display_set_buffers(s_lv_disp, s_buf1, s_buf2,
                           buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    // Configure SPI device for LCD (after LVGL display is created)
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = BSP_LCD_DC,
        .cs_gpio_num = BSP_LCD_SPI_CS,
        .pclk_hz = BSP_LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .on_color_trans_done = panel_io_done_cb,
        .user_ctx = s_lv_disp,  // Pass display handle for flush ready
    };

    ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BSP_LCD_SPI_HOST,
                                    &io_config, &s_panel_io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create panel IO: %s", esp_err_to_name(ret));
        goto err_cleanup;
    }

    // Create LCD panel (using generic ST7789 driver as base)
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BSP_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };

    ret = esp_lcd_new_panel_st7789(s_panel_io, &panel_config, &s_panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create panel: %s", esp_err_to_name(ret));
        goto err_cleanup;
    }

    // Reset and initialize panel
    esp_lcd_panel_reset(s_panel);
    send_init_commands();
    esp_lcd_panel_init(s_panel);

    // Configure display orientation
    esp_lcd_panel_mirror(s_panel, cfg.mirror_x, cfg.mirror_y);
    esp_lcd_panel_swap_xy(s_panel, cfg.swap_xy);

    // Turn display on
    esp_lcd_panel_disp_on_off(s_panel, true);

    // Set flush callback and user data (LVGL 9.x API)
    lv_display_set_flush_cb(s_lv_disp, lvgl_flush_cb);
    lv_display_set_user_data(s_lv_disp, s_panel);

    ESP_LOGI(TAG, "Display initialized successfully");
    return ESP_OK;

err_cleanup:
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

esp_err_t bsp_display_deinit(void)
{
    if (s_panel == NULL) {
        return ESP_OK;
    }

    bsp_display_stop_tick_timer();

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

lv_disp_t *bsp_display_get_lv_disp(void)
{
    return s_lv_disp;
}

esp_err_t bsp_display_set_brightness(uint8_t brightness)
{
    if (brightness > 100) {
        brightness = 100;
    }
    s_brightness = brightness;

    // Set LEDC duty cycle (0-255)
    uint32_t duty = (brightness * 255) / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

    ESP_LOGI(TAG, "Brightness set to %d%%", brightness);
    return ESP_OK;
}

uint8_t bsp_display_get_brightness(void)
{
    return s_brightness;
}

esp_err_t bsp_display_power(bool on)
{
    if (s_panel == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_display_on = on;
    esp_lcd_panel_disp_on_off(s_panel, on);

    if (!on) {
        // Turn off backlight
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    } else {
        // Restore backlight
        bsp_display_set_brightness(s_brightness);
    }

    ESP_LOGI(TAG, "Display power %s", on ? "on" : "off");
    return ESP_OK;
}

esp_err_t bsp_display_set_rotation(uint16_t rotation)
{
    if (s_panel == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    bool swap_xy = false;
    bool mirror_x = false;
    bool mirror_y = false;

    switch (rotation) {
        case 0:
            break;
        case 90:
            swap_xy = true;
            mirror_x = true;
            break;
        case 180:
            mirror_x = true;
            mirror_y = true;
            break;
        case 270:
            swap_xy = true;
            mirror_y = true;
            break;
        default:
            ESP_LOGW(TAG, "Invalid rotation: %d", rotation);
            return ESP_ERR_INVALID_ARG;
    }

    esp_lcd_panel_swap_xy(s_panel, swap_xy);
    esp_lcd_panel_mirror(s_panel, mirror_x, mirror_y);

    ESP_LOGI(TAG, "Rotation set to %d degrees", rotation);
    return ESP_OK;
}

bool bsp_display_lock(int timeout_ms)
{
    if (s_lvgl_mutex == NULL) {
        return false;
    }

    TickType_t ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(s_lvgl_mutex, ticks) == pdTRUE;
}

void bsp_display_unlock(void)
{
    if (s_lvgl_mutex != NULL) {
        xSemaphoreGiveRecursive(s_lvgl_mutex);
    }
}

void bsp_display_refresh(void)
{
    if (bsp_display_lock(100)) {
        lv_refr_now(s_lv_disp);
        bsp_display_unlock();
    }
}

esp_err_t bsp_display_start_tick_timer(void)
{
    if (s_lvgl_tick_timer != NULL) {
        return ESP_OK;  // Already running
    }

    const esp_timer_create_args_t timer_args = {
        .callback = lvgl_tick_cb,
        .name = "lvgl_tick",
    };

    esp_err_t ret = esp_timer_create(&timer_args, &s_lvgl_tick_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LVGL tick timer: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_timer_start_periodic(s_lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start LVGL tick timer: %s", esp_err_to_name(ret));
        esp_timer_delete(s_lvgl_tick_timer);
        s_lvgl_tick_timer = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "LVGL tick timer started");
    return ESP_OK;
}

esp_err_t bsp_display_stop_tick_timer(void)
{
    if (s_lvgl_tick_timer == NULL) {
        return ESP_OK;
    }

    esp_timer_stop(s_lvgl_tick_timer);
    esp_timer_delete(s_lvgl_tick_timer);
    s_lvgl_tick_timer = NULL;

    ESP_LOGI(TAG, "LVGL tick timer stopped");
    return ESP_OK;
}

esp_lcd_panel_handle_t bsp_display_get_panel_handle(void)
{
    return s_panel;
}

void bsp_display_fill_color(uint16_t color)
{
    if (s_panel == NULL) return;

    // Create a line buffer
    uint16_t *line = heap_caps_malloc(BSP_LCD_WIDTH * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (line == NULL) {
        ESP_LOGE(TAG, "Failed to allocate line buffer");
        return;
    }

    // Fill line with color
    for (int i = 0; i < BSP_LCD_WIDTH; i++) {
        line[i] = color;
    }

    // Draw line by line
    for (int y = 0; y < BSP_LCD_HEIGHT; y++) {
        esp_lcd_panel_draw_bitmap(s_panel, 0, y, BSP_LCD_WIDTH, y + 1, line);
    }

    heap_caps_free(line);
}

void bsp_display_draw_bitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                             const uint16_t *data)
{
    if (s_panel == NULL || data == NULL) return;

    esp_lcd_panel_draw_bitmap(s_panel, x, y, x + w, y + h, data);
}
