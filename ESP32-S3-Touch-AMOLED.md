# 微雪 ESP32-S3-Touch-AMOLED-1.75 开发板规格参数

## 产品概述

ESP32-S3-Touch-AMOLED-1.75 是微雪 (Waveshare) 设计的高性能、高集成微控制器开发板。在紧凑的板型下，集成了 1.75 英寸电容触摸圆形 AMOLED 屏、电源管理芯片、六轴传感器、RTC、TF 卡槽、双麦克风阵列、扬声器等外设，支持 AI 语音交互。

---

## 核心参数

### 处理器

| 参数 | 规格 |
|------|------|
| 芯片型号 | ESP32-S3R8 |
| 处理器架构 | Xtensa® 32-bit LX7 双核 |
| 主频 | 最高 240 MHz |
| 内置 SRAM | 512 KB |
| 内置 ROM | 384 KB |
| 叠封 PSRAM | 8 MB (Octal, 80MHz) |
| 外置 Flash | 16 MB |

### 无线通信

| 参数 | 规格 |
|------|------|
| Wi-Fi | 2.4 GHz (802.11 b/g/n)，40MHz 带宽 |
| 蓝牙 | Bluetooth 5 (LE) + Bluetooth Mesh |
| 天线 | 板载 PCB 天线 |

---

## 显示屏

| 参数 | 规格 |
|------|------|
| 屏幕类型 | AMOLED (有源矩阵有机发光二极管) |
| 屏幕尺寸 | 1.75 英寸 (约 45mm 直径) |
| 屏幕形状 | 圆形 |
| 分辨率 | 466 × 466 像素 |
| 色彩深度 | 16.7M 色 (24-bit) |
| 驱动芯片 | SH8601 |
| 接口类型 | QSPI |
| 特性 | 高对比度、广视角、响应快、低功耗 |

### 触摸屏

| 参数 | 规格 |
|------|------|
| 触摸类型 | 电容式触摸屏 |
| 触摸芯片 | CST92xx (I2C 地址 0x5A) |
| 接口类型 | I2C |
| 支持手势 | 单点触摸 |

---

## 音频系统

| 参数 | 规格 |
|------|------|
| 音频编解码芯片 | ES8311 |
| 回声消除芯片 | ES7210 |
| 麦克风 | 板载双数字麦克风阵列 |
| 扬声器 | 支持外接 8Ω 2W 扬声器 |
| 音频接口 | I2S |
| 功能支持 | 降噪、回声消除、语音唤醒、AI语音交互 |

---

## 传感器

### 六轴惯性测量单元 (IMU)

| 参数 | 规格 |
|------|------|
| 芯片型号 | QMI8658 |
| 加速度计 | 3轴 |
| 陀螺仪 | 3轴 |
| 接口 | I2C |
| 应用 | 运动检测、手势识别、计步 |

---

## 电源管理

| 参数 | 规格 |
|------|------|
| 电源管理芯片 | AXP2101 |
| USB 输入电压 | 5V (Type-C) |
| 电池接口 | 3.7V MX1.25 锂电池接口 |
| 电池充电 | 支持锂电池充放电管理 |
| RTC 芯片 | PCF85063 |
| RTC 供电 | 通过 AXP2101 由锂电池供电，支持不间断供电 |

---

## GPIO 引脚定义

### I2C 总线

| 功能 | GPIO |
|------|------|
| I2C_SDA | GPIO15 |
| I2C_SCL | GPIO14 |
| TOUCH_RST | GPIO40 |
| TOUCH_INT | GPIO11 |

### QSPI 显示接口 (SH8601)

| 功能 | GPIO |
|------|------|
| LCD_CLK | GPIO38 |
| LCD_D0 | GPIO4 |
| LCD_D1 | GPIO5 |
| LCD_D2 | GPIO6 |
| LCD_D3 | GPIO7 |
| LCD_CS | GPIO12 |
| LCD_RST | GPIO39 |

### I2S 音频接口

| 功能 | GPIO |
|------|------|
| I2S_MCLK | GPIO42 |
| I2S_BCLK | GPIO9 |
| I2S_LRCLK | GPIO45 |
| I2S_DOUT (扬声器) | GPIO8 |
| I2S_DIN (麦克风) | GPIO10 |
| SPEAKER_EN | GPIO46 |

### 扩展接口

| 功能 | 说明 |
|------|------|
| GPIO 扩展芯片 | TCA9554 (I2C) |
| 外部接口 | 8PIN 2.54mm 排针：3×GPIO + 1×UART |
| 预留焊盘 | I2C + 3×扩展IO |

---

## 存储扩展

| 参数 | 规格 |
|------|------|
| TF 卡槽 | 板载 Micro SD 卡槽 |
| 接口类型 | SDMMC |
| 用途 | 扩展存储、数据记录、媒体播放 |

---

## 物理规格

| 参数 | 规格 |
|------|------|
| 外形 | 圆形（适配智能手表造型） |
| 显示屏直径 | 约 45mm |
| 接口 | USB Type-C |
| 按键 | PWR 电源键、BOOT 下载键 |

### 按键功能

| 按键 | 功能 |
|------|------|
| BOOT | 上电时按住进入下载模式；正常运行时可检测 GPIO0 电平 |
| PWR | 开机时长按 6s 关机；关机时点击开机 |

---

## 开发支持

### 开发框架

| 框架 | 支持情况 |
|------|----------|
| ESP-IDF | ✅ 支持 |
| Arduino (arduino-esp32) | ✅ 支持 |
| PlatformIO | ✅ 支持 |
| ESPHome | ✅ 支持 |

### AI 语音平台支持

- DeepSeek
- 豆包 (Doubao)
- 文心一言 (ERNIE Bot)
- ChatGPT / OpenAI
- 小智 AI

---

## 产品变体

| 型号 | 说明 |
|------|------|
| ESP32-S3-Touch-AMOLED-1.75 | 标准版 |
| ESP32-S3-Touch-AMOLED-1.75-B | 带外壳版本 |
| ESP32-S3-Touch-AMOLED-1.75-G | GPS 版本 (内置 LC76G 模块 + GNSS 陶瓷天线) |

---

## 板载芯片清单

| 芯片 | 功能 |
|------|------|
| ESP32-S3R8 | 主控 MCU |
| SH8601 | AMOLED 显示驱动 |
| CST92xx | 电容触摸控制器 |
| ES8311 | 音频编解码器 |
| ES7210 | 回声消除 |
| QMI8658 | 六轴 IMU |
| AXP2101 | 电源管理 |
| PCF85063 | RTC 实时时钟 |
| TCA9554 | I2C GPIO 扩展器 |

---

## 参考资料

- [Waveshare 官方 Wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75)
- [Waveshare 产品页面](https://www.waveshare.com/esp32-s3-touch-amoled-1.75.htm)
- [ESPHome 设备配置](https://devices.esphome.io/devices/waveshare-esp32-s3-touch-amoled-1.75/)
- [Waveshare ESP32 Components (GitHub)](https://github.com/waveshareteam/Waveshare-ESP32-components)
- [ESP Component Registry](https://components.espressif.com/components/waveshare/esp32_s3_touch_amoled_1_75)

---

*文档更新日期: 2025-12-03*
