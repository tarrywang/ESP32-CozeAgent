/*
 * Status Display Implementation
 * System status pages with touch switching for ESP32-S3-Touch-AMOLED-1.75
 */

#include "status_display.h"
#include <stdio.h>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_chip_info.h"
#include "esp_system.h"
#include "lvgl.h"
#include "esp_lcd_sh8601.h"
#include "SensorLib.h"
#include "TouchDrvCST92xx.h"

static const char *TAG = "status_display";

// Display configuration
#define LCD_HOST            SPI2_HOST
#define LCD_H_RES           466
#define LCD_V_RES           466
#define LCD_BIT_PER_PIXEL   16

// Pin definitions
#define PIN_LCD_CS          GPIO_NUM_12
#define PIN_LCD_PCLK        GPIO_NUM_38
#define PIN_LCD_DATA0       GPIO_NUM_4
#define PIN_LCD_DATA1       GPIO_NUM_5
#define PIN_LCD_DATA2       GPIO_NUM_6
#define PIN_LCD_DATA3       GPIO_NUM_7
#define PIN_LCD_RST         GPIO_NUM_39

// Touch pins
#define PIN_TOUCH_SCL       GPIO_NUM_14
#define PIN_TOUCH_SDA       GPIO_NUM_15
#define PIN_TOUCH_RST       ((gpio_num_t)40)
#define PIN_TOUCH_INT       GPIO_NUM_11
#define TOUCH_ADDR          0x5A

// LVGL configuration
#define LVGL_BUF_HEIGHT     (LCD_V_RES / 4)
#define LVGL_TICK_MS        2
#define LVGL_TASK_STACK     (8 * 1024)
#define LVGL_TASK_PRIO      2

// Auto page rotation interval (5 seconds)
#define PAGE_AUTO_SWITCH_MS 5000

// Static variables
static SemaphoreHandle_t s_lvgl_mux = NULL;
static TouchDrvCST92xx s_touch;
static int16_t s_touch_x[5], s_touch_y[5];
static status_page_t s_current_page = STATUS_PAGE_SYSTEM;
static audio_status_t s_audio_status = {0};
static lv_obj_t *s_pages[STATUS_PAGE_MAX] = {NULL};
static lv_obj_t *s_page_indicator = NULL;
static int64_t s_last_page_switch_time = 0;

// UI elements for each page
static struct {
    lv_obj_t *heap_label;
    lv_obj_t *cpu_label;
    lv_obj_t *uptime_label;
    lv_obj_t *chip_label;
} s_system_ui;

static struct {
    lv_obj_t *mode_label;
    lv_obj_t *sample_rate_label;
    lv_obj_t *volume_label;
    lv_obj_t *status_label;
    lv_obj_t *volume_bar;
} s_audio_ui;

// LCD init commands for SH8601
static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x06, 0x01, 0xD7}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xD1}, 4, 600},
    {0x11, NULL, 0, 600},
    {0x29, NULL, 0, 0},
};

// Forward declarations
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map);
static void lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data);
static void lvgl_port_task(void *arg);
static void create_system_page(lv_obj_t *parent);
static void create_audio_page(lv_obj_t *parent);
static void create_wifi_page(lv_obj_t *parent);
static void page_switch_event_cb(lv_event_t *e);

static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io,
                                     esp_lcd_panel_io_event_data_t *edata,
                                     void *user_ctx)
{
    lv_disp_drv_t *disp_driver = (lv_disp_drv_t *)user_ctx;
    lv_disp_flush_ready(disp_driver);
    return false;
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1, color_map);
}

static void lvgl_rounder_cb(lv_disp_drv_t *drv, lv_area_t *area)
{
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}

static void lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    uint8_t touched = s_touch.getPoint(s_touch_x, s_touch_y, 2);
    if (touched) {
        data->point.x = s_touch_x[0];
        data->point.y = s_touch_y[0];
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void increase_lvgl_tick(void *arg)
{
    lv_tick_inc(LVGL_TICK_MS);
}

static esp_err_t init_touch(void)
{
    // Touch uses the same I2C bus as codec (I2C_NUM_0, already initialized)
    // Just configure the touch driver to use the existing bus
    s_touch.setPins((int)PIN_TOUCH_RST, (int)PIN_TOUCH_INT);
    // Use legacy API: begin(i2c_port_t port_num, uint8_t addr, int sda, int scl)
    s_touch.begin(I2C_NUM_0, TOUCH_ADDR, (int)PIN_TOUCH_SDA, (int)PIN_TOUCH_SCL);
    s_touch.reset();
    s_touch.setMaxCoordinates(LCD_H_RES, LCD_V_RES);
    s_touch.setMirrorXY(true, true);

    ESP_LOGI(TAG, "Touch initialized");
    return ESP_OK;
}

static void lvgl_port_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL task started");
    uint32_t delay_ms = 10;

    while (1) {
        if (status_display_lock(-1)) {
            delay_ms = lv_timer_handler();
            status_display_unlock();
        }

        if (delay_ms > 500) delay_ms = 500;
        if (delay_ms < 1) delay_ms = 1;

        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

// Create page indicator dots
static void create_page_indicator(lv_obj_t *parent)
{
    s_page_indicator = lv_obj_create(parent);
    lv_obj_set_size(s_page_indicator, LCD_H_RES, 30);
    lv_obj_align(s_page_indicator, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_opa(s_page_indicator, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_page_indicator, 0, 0);
    lv_obj_set_flex_flow(s_page_indicator, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_page_indicator, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for (int i = 0; i < STATUS_PAGE_MAX; i++) {
        lv_obj_t *dot = lv_obj_create(s_page_indicator);
        lv_obj_set_size(dot, 12, 12);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);

        if (i == s_current_page) {
            lv_obj_set_style_bg_color(dot, lv_color_hex(0x6699AA), 0);
        } else {
            lv_obj_set_style_bg_color(dot, lv_color_hex(0x404040), 0);
        }
    }
}

static void update_page_indicator(void)
{
    if (!s_page_indicator) return;

    uint32_t cnt = lv_obj_get_child_cnt(s_page_indicator);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t *dot = lv_obj_get_child(s_page_indicator, i);
        if (i == (uint32_t)s_current_page) {
            lv_obj_set_style_bg_color(dot, lv_color_hex(0x6699AA), 0);
        } else {
            lv_obj_set_style_bg_color(dot, lv_color_hex(0x404040), 0);
        }
    }
}

static lv_obj_t* create_info_card(lv_obj_t *parent, const char *title, const char *icon)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, LCD_H_RES - 40, 80);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_radius(card, 15, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 15, 0);

    // Icon/Title
    lv_obj_t *title_label = lv_label_create(card);
    lv_label_set_text_fmt(title_label, "%s %s", icon, title);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0x808080), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 0);

    // Value label
    lv_obj_t *value_label = lv_label_create(card);
    lv_label_set_text(value_label, "--");
    lv_obj_set_style_text_color(value_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(value_label, &lv_font_montserrat_14, 0);
    lv_obj_align(value_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    return value_label;
}

static void create_system_page(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x0A0A0A), 0);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(parent, 30, 0);
    lv_obj_set_style_pad_gap(parent, 15, 0);

    // Title
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "System Status");
    lv_obj_set_style_text_color(title, lv_color_hex(0x6699AA), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);

    // Chip info card
    s_system_ui.chip_label = create_info_card(parent, "Chip", LV_SYMBOL_HOME);

    // Heap card
    s_system_ui.heap_label = create_info_card(parent, "Free Heap", LV_SYMBOL_REFRESH);

    // Uptime card
    s_system_ui.uptime_label = create_info_card(parent, "Uptime", LV_SYMBOL_LOOP);

    // Set initial chip info
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    lv_label_set_text_fmt(s_system_ui.chip_label, "ESP32-S3 %d cores", chip_info.cores);
}

static void create_audio_page(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x0A0A0A), 0);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(parent, 30, 0);
    lv_obj_set_style_pad_gap(parent, 15, 0);

    // Title
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Audio Status");
    lv_obj_set_style_text_color(title, lv_color_hex(0x996666), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);

    // Mode card
    s_audio_ui.mode_label = create_info_card(parent, "Mode", LV_SYMBOL_AUDIO);

    // Sample rate card
    s_audio_ui.sample_rate_label = create_info_card(parent, "Sample Rate", LV_SYMBOL_SETTINGS);

    // Volume card with bar
    lv_obj_t *vol_card = lv_obj_create(parent);
    lv_obj_set_size(vol_card, LCD_H_RES - 40, 100);
    lv_obj_set_style_bg_color(vol_card, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_radius(vol_card, 15, 0);
    lv_obj_set_style_border_width(vol_card, 0, 0);
    lv_obj_set_style_pad_all(vol_card, 15, 0);

    lv_obj_t *vol_title = lv_label_create(vol_card);
    lv_label_set_text(vol_title, LV_SYMBOL_VOLUME_MAX " Volume");
    lv_obj_set_style_text_color(vol_title, lv_color_hex(0x808080), 0);
    lv_obj_align(vol_title, LV_ALIGN_TOP_LEFT, 0, 0);

    s_audio_ui.volume_label = lv_label_create(vol_card);
    lv_label_set_text(s_audio_ui.volume_label, "0%");
    lv_obj_set_style_text_color(s_audio_ui.volume_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_audio_ui.volume_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_audio_ui.volume_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    s_audio_ui.volume_bar = lv_bar_create(vol_card);
    lv_obj_set_size(s_audio_ui.volume_bar, LCD_H_RES - 80, 20);
    lv_obj_align(s_audio_ui.volume_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_bar_set_range(s_audio_ui.volume_bar, 0, 100);
    lv_bar_set_value(s_audio_ui.volume_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_audio_ui.volume_bar, lv_color_hex(0x404040), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_audio_ui.volume_bar, lv_color_hex(0x6699AA), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_audio_ui.volume_bar, 10, LV_PART_MAIN);
    lv_obj_set_style_radius(s_audio_ui.volume_bar, 10, LV_PART_INDICATOR);

    // Status indicator
    s_audio_ui.status_label = create_info_card(parent, "Status", LV_SYMBOL_PLAY);
}

static void create_wifi_page(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x0A0A0A), 0);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Title
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "WiFi Status");
    lv_obj_set_style_text_color(title, lv_color_hex(0x669966), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);

    // Placeholder
    lv_obj_t *placeholder = lv_label_create(parent);
    lv_label_set_text(placeholder, LV_SYMBOL_WIFI "\nNot Connected");
    lv_obj_set_style_text_color(placeholder, lv_color_hex(0x606060), 0);
    lv_obj_set_style_text_font(placeholder, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(placeholder, LV_TEXT_ALIGN_CENTER, 0);
}

static void page_switch_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());

        if (dir == LV_DIR_LEFT) {
            status_display_next_page();
        } else if (dir == LV_DIR_RIGHT) {
            int next = (int)s_current_page - 1;
            if (next < 0) next = STATUS_PAGE_MAX - 1;
            status_display_switch_page((status_page_t)next);
        }
    }
    // Removed click-to-switch - only swipe to change pages
}

static void create_ui(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0A0A0A), 0);

    // Create container for pages
    lv_obj_t *cont = lv_obj_create(scr);
    lv_obj_set_size(cont, LCD_H_RES, LCD_V_RES - 50);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_add_event_cb(cont, page_switch_event_cb, LV_EVENT_ALL, NULL);

    // Create pages
    for (int i = 0; i < STATUS_PAGE_MAX; i++) {
        s_pages[i] = lv_obj_create(cont);
        lv_obj_set_size(s_pages[i], LCD_H_RES, LCD_V_RES - 50);
        lv_obj_set_style_border_width(s_pages[i], 0, 0);
        lv_obj_set_style_pad_all(s_pages[i], 0, 0);
        lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
    }

    create_system_page(s_pages[STATUS_PAGE_SYSTEM]);
    create_audio_page(s_pages[STATUS_PAGE_AUDIO]);
    create_wifi_page(s_pages[STATUS_PAGE_WIFI]);

    // Show first page
    lv_obj_clear_flag(s_pages[STATUS_PAGE_SYSTEM], LV_OBJ_FLAG_HIDDEN);

    // Create page indicator
    create_page_indicator(scr);
}

extern "C" esp_err_t status_display_init(void)
{
    static lv_disp_draw_buf_t disp_buf;
    static lv_disp_drv_t disp_drv;

    ESP_LOGI(TAG, "Initializing display...");

    // Initialize SPI bus
    spi_bus_config_t buscfg = {};
    buscfg.sclk_io_num = PIN_LCD_PCLK;
    buscfg.data0_io_num = PIN_LCD_DATA0;
    buscfg.data1_io_num = PIN_LCD_DATA1;
    buscfg.data2_io_num = PIN_LCD_DATA2;
    buscfg.data3_io_num = PIN_LCD_DATA3;
    buscfg.max_transfer_sz = LCD_H_RES * LCD_V_RES * sizeof(uint16_t);
    buscfg.flags = SPICOMMON_BUSFLAG_QUAD;
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // Panel IO
    esp_lcd_panel_io_handle_t io_handle = NULL;
    const esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG(
        PIN_LCD_CS, notify_lvgl_flush_ready, &disp_drv);

    sh8601_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags = {.use_qspi_interface = 1},
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    // Panel handle
    esp_lcd_panel_handle_t panel_handle = NULL;
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .vendor_config = &vendor_config,
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    // Initialize touch
    ESP_ERROR_CHECK(init_touch());

    // Initialize LVGL
    ESP_LOGI(TAG, "Initializing LVGL...");
    lv_init();

    // Allocate draw buffers
    lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(
        LCD_H_RES * LVGL_BUF_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_DMA);
    lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(
        LCD_H_RES * LVGL_BUF_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_DMA);

    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "Failed to allocate LVGL buffers");
        return ESP_ERR_NO_MEM;
    }

    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, LCD_H_RES * LVGL_BUF_HEIGHT);

    // Register display driver
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_H_RES;
    disp_drv.ver_res = LCD_V_RES;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.rounder_cb = lvgl_rounder_cb;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.user_data = panel_handle;
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);

    // Register input device
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.disp = disp;
    indev_drv.read_cb = lvgl_touch_cb;
    lv_indev_drv_register(&indev_drv);

    // Create tick timer
    const esp_timer_create_args_t tick_timer_args = {
        .callback = &increase_lvgl_tick,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&tick_timer_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, LVGL_TICK_MS * 1000));

    // Create mutex and task
    s_lvgl_mux = xSemaphoreCreateMutex();
    xTaskCreate(lvgl_port_task, "lvgl", LVGL_TASK_STACK, NULL, LVGL_TASK_PRIO, NULL);

    // Create UI
    if (status_display_lock(-1)) {
        create_ui();
        status_display_unlock();
    }

    ESP_LOGI(TAG, "Display initialized successfully");
    return ESP_OK;
}

extern "C" void status_display_switch_page(status_page_t page)
{
    if (page >= STATUS_PAGE_MAX) return;

    if (status_display_lock(100)) {
        // Hide current page
        lv_obj_add_flag(s_pages[s_current_page], LV_OBJ_FLAG_HIDDEN);

        // Show new page
        s_current_page = page;
        lv_obj_clear_flag(s_pages[s_current_page], LV_OBJ_FLAG_HIDDEN);

        // Update indicator
        update_page_indicator();

        // Reset auto-switch timer
        s_last_page_switch_time = esp_timer_get_time() / 1000;

        status_display_unlock();
    }
}

extern "C" void status_display_next_page(void)
{
    status_page_t next = (status_page_t)((s_current_page + 1) % STATUS_PAGE_MAX);
    status_display_switch_page(next);
}

extern "C" void status_display_update(void)
{
    // Check for auto page switch (every 5 seconds)
    int64_t now_ms = esp_timer_get_time() / 1000;
    if (s_last_page_switch_time == 0) {
        s_last_page_switch_time = now_ms;
    }
    if ((now_ms - s_last_page_switch_time) >= PAGE_AUTO_SWITCH_MS) {
        status_display_next_page();
        // Note: next_page calls switch_page which resets the timer
    }

    if (!status_display_lock(50)) return;

    // Update system page
    size_t free_heap = esp_get_free_heap_size();
    size_t min_heap = esp_get_minimum_free_heap_size();
    int64_t uptime_us = esp_timer_get_time();
    int uptime_sec = uptime_us / 1000000;
    int hours = uptime_sec / 3600;
    int mins = (uptime_sec % 3600) / 60;
    int secs = uptime_sec % 60;

    if (s_system_ui.heap_label) {
        lv_label_set_text_fmt(s_system_ui.heap_label, "%d KB (min: %d KB)",
                              free_heap / 1024, min_heap / 1024);
    }

    if (s_system_ui.uptime_label) {
        lv_label_set_text_fmt(s_system_ui.uptime_label, "%02d:%02d:%02d", hours, mins, secs);
    }

    // Update audio page
    if (s_audio_ui.mode_label) {
        lv_label_set_text(s_audio_ui.mode_label, s_audio_status.mode ? s_audio_status.mode : "Unknown");
    }

    if (s_audio_ui.sample_rate_label) {
        lv_label_set_text_fmt(s_audio_ui.sample_rate_label, "%d Hz", s_audio_status.sample_rate);
    }

    if (s_audio_ui.volume_label) {
        lv_label_set_text_fmt(s_audio_ui.volume_label, "%d%%", s_audio_status.volume);
    }

    if (s_audio_ui.volume_bar) {
        lv_bar_set_value(s_audio_ui.volume_bar, s_audio_status.volume, LV_ANIM_ON);
    }

    if (s_audio_ui.status_label) {
        if (s_audio_status.is_playing) {
            lv_label_set_text(s_audio_ui.status_label, "Playing...");
        } else if (s_audio_status.is_recording) {
            lv_label_set_text(s_audio_ui.status_label, "Recording...");
        } else {
            lv_label_set_text(s_audio_ui.status_label, "Idle");
        }
    }

    status_display_unlock();
}

extern "C" void status_display_set_audio_status(const audio_status_t *status)
{
    if (status) {
        memcpy(&s_audio_status, status, sizeof(audio_status_t));
    }
}

extern "C" bool status_display_lock(int timeout_ms)
{
    if (!s_lvgl_mux) return false;
    TickType_t ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(s_lvgl_mux, ticks) == pdTRUE;
}

extern "C" void status_display_unlock(void)
{
    if (s_lvgl_mux) {
        xSemaphoreGive(s_lvgl_mux);
    }
}
