#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "freertos/queue.h"
#include "driver/i2c.h"
#include "SensorLib.h"
#include "SensorPCF85063.hpp" // Ensure this path is correct

// I2C configuration
#define I2C_MASTER_NUM (i2c_port_t) 1
#define I2C_MASTER_FREQ_HZ 100000 /*!< I2C master clock frequency */
#define I2C_MASTER_SDA_IO (gpio_num_t) 15
#define I2C_MASTER_SCL_IO (gpio_num_t) 14

#define I2C_MASTER_TX_BUF_DISABLE 0 /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE 0 /*!< I2C master doesn't need buffer */
#define I2C_MASTER_TIMEOUT_MS 1000

SensorPCF85063 rtc;

static const char *TAG = "QMI8658"; // Define a tag for logging

esp_err_t i2c_init(void)
{
    i2c_config_t i2c_conf;
    memset(&i2c_conf, 0, sizeof(i2c_conf));
    i2c_conf.mode = I2C_MODE_MASTER;
    i2c_conf.sda_io_num = I2C_MASTER_SDA_IO;
    i2c_conf.scl_io_num = I2C_MASTER_SCL_IO;
    i2c_conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_conf.master.clk_speed = I2C_MASTER_FREQ_HZ;
    i2c_param_config(I2C_MASTER_NUM, &i2c_conf);
    return i2c_driver_install(I2C_MASTER_NUM, i2c_conf.mode, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
}
void read_sensor_data(void *arg); // Function declaration

void setup_sensor()
{

    if (!rtc.begin(I2C_MASTER_NUM, PCF85063_SLAVE_ADDRESS, I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO))
    {
        ESP_LOGE(TAG, "Failed to find PCF8563 - check your wiring!");
        while (1)
        {
            delay(1000);
        }
    }

    uint16_t year = 2023;
    uint8_t month = 9;
    uint8_t day = 7;
    uint8_t hour = 11;
    uint8_t minute = 24;
    uint8_t second = 30;

    rtc.setDateTime(year, month, day, hour, minute, second);
}

extern "C" void app_main()
{   
    ESP_ERROR_CHECK(i2c_init());

    setup_sensor();
    xTaskCreate(read_sensor_data, "sensor_read_task", 4096, NULL, 10, NULL);
}

void read_sensor_data(void *arg)
{
    while (1)
    {
        RTC_DateTime datetime = rtc.getDateTime();

        ESP_LOGI(TAG, "Year: %d, Month: %d, Day: %d, Hour: %d, Minute: %d, Sec: %d",
                 datetime.year, datetime.month, datetime.day,
                 datetime.hour, datetime.minute, datetime.second);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
