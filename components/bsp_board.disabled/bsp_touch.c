/**
 * @file bsp_touch.c
 * @brief Touch controller driver implementation for CST816S
 *
 * Capacitive touch controller for Waveshare ESP32-S3 1.75-inch AMOLED.
 * CST816S is a single-touch controller with gesture support.
 * Updated for LVGL 9.x API.
 */

#include "bsp_touch.h"
#include "bsp_board.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "driver/gpio.h"

static const char *TAG = "BSP_TOUCH";

// ============================================
// CST816S Register Definitions
// ============================================

#define CST816S_REG_GESTURE     0x01    // Gesture register
#define CST816S_REG_FINGER_NUM  0x02    // Number of touch points
#define CST816S_REG_XH          0x03    // X position high (4 bits)
#define CST816S_REG_XL          0x04    // X position low (8 bits)
#define CST816S_REG_YH          0x05    // Y position high (4 bits)
#define CST816S_REG_YL          0x06    // Y position low (8 bits)
#define CST816S_REG_CHIP_ID     0xA7    // Chip ID
#define CST816S_REG_FW_VERSION  0xA9    // Firmware version
#define CST816S_REG_SLEEP       0xA5    // Sleep mode control (write 0x03)

// CST816S Gesture codes
#define CST816S_GESTURE_NONE        0x00
#define CST816S_GESTURE_SWIPE_UP    0x01
#define CST816S_GESTURE_SWIPE_DOWN  0x02
#define CST816S_GESTURE_SWIPE_LEFT  0x03
#define CST816S_GESTURE_SWIPE_RIGHT 0x04
#define CST816S_GESTURE_SINGLE_TAP  0x05
#define CST816S_GESTURE_DOUBLE_TAP  0x0B
#define CST816S_GESTURE_LONG_PRESS  0x0C

// ============================================
// Private Variables
// ============================================

static bool s_touch_initialized = false;
static bsp_touch_config_t s_touch_config;
static bsp_touch_callback_t s_touch_callback = NULL;
static lv_indev_t *s_lv_indev = NULL;
static SemaphoreHandle_t s_touch_mutex = NULL;

static bsp_touch_data_t s_last_touch_data;
static bsp_touch_gesture_t s_last_gesture = BSP_TOUCH_GESTURE_NONE;

// ============================================
// Private Functions
// ============================================

/**
 * @brief Read bytes from I2C device
 */
static esp_err_t i2c_read_reg(uint8_t reg, uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BSP_TOUCH_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BSP_TOUCH_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    if (len > 1) {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(BSP_TOUCH_I2C_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    return ret;
}

/**
 * @brief Write byte to I2C device
 */
static esp_err_t i2c_write_reg(uint8_t reg, uint8_t data)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BSP_TOUCH_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, data, true);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(BSP_TOUCH_I2C_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    return ret;
}

/**
 * @brief Detect gesture from gesture register
 */
static bsp_touch_gesture_t detect_gesture(uint8_t gesture_code)
{
    switch (gesture_code) {
        case CST816S_GESTURE_SWIPE_UP:    return BSP_TOUCH_GESTURE_SWIPE_UP;
        case CST816S_GESTURE_SWIPE_DOWN:  return BSP_TOUCH_GESTURE_SWIPE_DOWN;
        case CST816S_GESTURE_SWIPE_LEFT:  return BSP_TOUCH_GESTURE_SWIPE_LEFT;
        case CST816S_GESTURE_SWIPE_RIGHT: return BSP_TOUCH_GESTURE_SWIPE_RIGHT;
        case CST816S_GESTURE_SINGLE_TAP:  return BSP_TOUCH_GESTURE_TAP;
        case CST816S_GESTURE_DOUBLE_TAP:  return BSP_TOUCH_GESTURE_DOUBLE_TAP;
        case CST816S_GESTURE_LONG_PRESS:  return BSP_TOUCH_GESTURE_LONG_PRESS;
        default: return BSP_TOUCH_GESTURE_NONE;
    }
}

// ============================================
// Public Functions
// ============================================

esp_err_t bsp_touch_init(const bsp_touch_config_t *config)
{
    if (s_touch_initialized) {
        ESP_LOGW(TAG, "Touch already initialized");
        return ESP_OK;
    }

    // Use default config if not provided
    if (config == NULL) {
        s_touch_config = (bsp_touch_config_t)BSP_TOUCH_DEFAULT_CONFIG();
    } else {
        s_touch_config = *config;
    }

    ESP_LOGI(TAG, "Initializing touch controller: %dx%d",
             s_touch_config.width, s_touch_config.height);

    // Create mutex
    s_touch_mutex = xSemaphoreCreateMutex();
    if (s_touch_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create touch mutex");
        return ESP_ERR_NO_MEM;
    }

    // Configure interrupt GPIO (if available)
    if (BSP_TOUCH_INT != GPIO_NUM_NC) {
        gpio_config_t int_gpio_conf = {
            .pin_bit_mask = (1ULL << BSP_TOUCH_INT),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_NEGEDGE,
        };
        gpio_config(&int_gpio_conf);
    }

    // Wait for touch controller to be ready
    vTaskDelay(pdMS_TO_TICKS(50));

    // Try to read chip ID (optional - some CST816S variants may not respond)
    uint8_t chip_id = 0;
    esp_err_t ret = i2c_read_reg(CST816S_REG_CHIP_ID, &chip_id, 1);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Touch chip ID: 0x%02X", chip_id);
    } else {
        // CST816S may not respond to ID read, but still work for touch
        ESP_LOGW(TAG, "Could not read chip ID (this may be normal): %s", esp_err_to_name(ret));
    }

    // Initialize LVGL input device (LVGL 9.x API)
    s_lv_indev = lv_indev_create();
    if (s_lv_indev == NULL) {
        ESP_LOGE(TAG, "Failed to create LVGL input device");
        vSemaphoreDelete(s_touch_mutex);
        s_touch_mutex = NULL;
        return ESP_FAIL;
    }

    lv_indev_set_type(s_lv_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_lv_indev, bsp_touch_lv_read_cb);

    memset(&s_last_touch_data, 0, sizeof(s_last_touch_data));
    s_touch_initialized = true;

    ESP_LOGI(TAG, "Touch controller initialized");
    return ESP_OK;
}

esp_err_t bsp_touch_deinit(void)
{
    if (!s_touch_initialized) {
        return ESP_OK;
    }

    // Put touch controller to sleep
    i2c_write_reg(CST816S_REG_SLEEP, 0x03);

    if (s_lv_indev) {
        lv_indev_delete(s_lv_indev);
        s_lv_indev = NULL;
    }

    if (s_touch_mutex) {
        vSemaphoreDelete(s_touch_mutex);
        s_touch_mutex = NULL;
    }

    s_touch_initialized = false;

    ESP_LOGI(TAG, "Touch controller deinitialized");
    return ESP_OK;
}

esp_err_t bsp_touch_read(bsp_touch_data_t *data)
{
    if (!s_touch_initialized || data == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_touch_mutex, portMAX_DELAY);

    // Read touch data: gesture(1), finger_num(1), xh(1), xl(1), yh(1), yl(1)
    uint8_t touch_data[6];
    esp_err_t ret = i2c_read_reg(CST816S_REG_GESTURE, touch_data, 6);
    if (ret != ESP_OK) {
        xSemaphoreGive(s_touch_mutex);
        memset(data, 0, sizeof(bsp_touch_data_t));
        return ret;
    }

    // Parse gesture
    s_last_gesture = detect_gesture(touch_data[0]);

    // Parse touch point count (CST816S only supports single touch)
    uint8_t point_count = touch_data[1] & 0x0F;
    if (point_count > 1) {
        point_count = 1;  // CST816S only supports single touch
    }

    data->point_count = point_count;

    if (point_count > 0) {
        // Parse X coordinate (12-bit: high 4 bits + low 8 bits)
        uint16_t x = ((touch_data[2] & 0x0F) << 8) | touch_data[3];
        // Parse Y coordinate (12-bit: high 4 bits + low 8 bits)
        uint16_t y = ((touch_data[4] & 0x0F) << 8) | touch_data[5];

        // Apply coordinate transformations
        if (s_touch_config.swap_xy) {
            uint16_t temp = x;
            x = y;
            y = temp;
        }
        if (s_touch_config.invert_x) {
            x = s_touch_config.width - 1 - x;
        }
        if (s_touch_config.invert_y) {
            y = s_touch_config.height - 1 - y;
        }

        // Clamp to valid range
        if (x >= s_touch_config.width) x = s_touch_config.width - 1;
        if (y >= s_touch_config.height) y = s_touch_config.height - 1;

        data->points[0].x = x;
        data->points[0].y = y;
        data->points[0].id = 0;
        data->points[0].pressure = 255;  // CST816S doesn't report pressure
        data->points[0].valid = true;
    }

    // Mark remaining points as invalid
    for (int i = point_count; i < BSP_TOUCH_MAX_POINTS; i++) {
        data->points[i].valid = false;
    }

    // Store last touch data
    memcpy(&s_last_touch_data, data, sizeof(bsp_touch_data_t));

    xSemaphoreGive(s_touch_mutex);

    // Call callback if registered
    if (s_touch_callback != NULL && (point_count > 0 || s_last_gesture != BSP_TOUCH_GESTURE_NONE)) {
        s_touch_callback(data, s_last_gesture);
    }

    return ESP_OK;
}

bool bsp_touch_is_pressed(void)
{
    bsp_touch_data_t data;
    if (bsp_touch_read(&data) == ESP_OK) {
        return data.point_count > 0;
    }
    return false;
}

esp_err_t bsp_touch_register_callback(bsp_touch_callback_t callback)
{
    s_touch_callback = callback;
    return ESP_OK;
}

bsp_touch_gesture_t bsp_touch_get_gesture(void)
{
    return s_last_gesture;
}

esp_err_t bsp_touch_set_sensitivity(uint8_t sensitivity)
{
    ESP_LOGI(TAG, "Setting touch sensitivity to %d (not supported on CST816S)", sensitivity);
    return ESP_OK;
}

esp_err_t bsp_touch_enable(bool enable)
{
    if (enable) {
        // Wake up - CST816S wakes on touch or I2C activity
        return ESP_OK;
    } else {
        return i2c_write_reg(CST816S_REG_SLEEP, 0x03);
    }
}

esp_err_t bsp_touch_reset(void)
{
    // CST816S software reset via sleep register
    esp_err_t ret = i2c_write_reg(CST816S_REG_SLEEP, 0x04);
    if (ret == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return ret;
}

lv_indev_t *bsp_touch_get_lv_indev(void)
{
    return s_lv_indev;
}

void bsp_touch_lv_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;  // Unused in LVGL 9.x
    bsp_touch_data_t touch_data;

    if (bsp_touch_read(&touch_data) == ESP_OK && touch_data.point_count > 0) {
        data->point.x = touch_data.points[0].x;
        data->point.y = touch_data.points[0].y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->point.x = s_last_touch_data.points[0].x;
        data->point.y = s_last_touch_data.points[0].y;
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

esp_err_t bsp_touch_calibrate(void)
{
    ESP_LOGI(TAG, "Touch calibration not implemented for capacitive touch");
    return ESP_OK;
}

uint16_t bsp_touch_get_firmware_version(void)
{
    uint8_t fw_ver = 0;
    if (i2c_read_reg(CST816S_REG_FW_VERSION, &fw_ver, 1) == ESP_OK) {
        return fw_ver;
    }
    return 0;
}
