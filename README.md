# ESP32-S3 Coze Voice Agent Device

A production-level voice assistant device powered by ESP32-S3, featuring real-time voice conversation with Coze AI Agent, AMOLED touch display, and dual-microphone array.

## Hardware Target

**Waveshare ESP32-S3 1.75-inch AMOLED Dev Board**
- ESP32-S3R8 dual-core LX7 @ 240 MHz
- 8MB PSRAM, 16MB Flash
- Built-in dual microphone array (digital I²S)
- 466×466 AMOLED display (SH8601 driver)
- Touch controller CST9217 (I²C)
- AXP2101 power management IC
- I²S speaker output (MAX98357A compatible)
- MicroSD slot
- User buttons: PWR/BOOT

## Features

- **Voice Interaction**: Real-time voice conversation with Coze AI Agent
- **Audio Processing**: AEC (Acoustic Echo Cancellation), NS (Noise Suppression), VAD (Voice Activity Detection)
- **Touch UI**: Beautiful LVGL-based interface with state transitions
- **State Machine**: IDLE → LISTENING → THINKING → SPEAKING
- **WebSocket Streaming**: Bidirectional audio/text streaming with Coze API

## Project Structure

```
├── CMakeLists.txt              # Root CMake configuration
├── sdkconfig.defaults          # Default SDK configuration
├── partitions.csv              # Flash partition table
├── main/
│   ├── CMakeLists.txt
│   └── app_main.c              # Application entry point
├── components/
│   ├── bsp_board/              # Board Support Package
│   │   ├── include/
│   │   │   ├── bsp_board.h
│   │   │   ├── bsp_i2s.h
│   │   │   ├── bsp_display.h
│   │   │   ├── bsp_touch.h
│   │   │   └── bsp_button.h
│   │   ├── bsp_board.c
│   │   ├── bsp_i2s.c
│   │   ├── bsp_display.c
│   │   ├── bsp_touch.c
│   │   ├── bsp_button.c
│   │   └── CMakeLists.txt
│   ├── audio_pipeline/         # Audio Processing Pipeline
│   │   ├── include/
│   │   │   ├── audio_pipeline.h
│   │   │   ├── audio_recorder.h
│   │   │   └── audio_player.h
│   │   ├── audio_pipeline.c
│   │   ├── audio_recorder.c
│   │   ├── audio_player.c
│   │   └── CMakeLists.txt
│   ├── coze_ws/                # Coze WebSocket Client
│   │   ├── include/
│   │   │   ├── coze_ws.h
│   │   │   └── coze_protocol.h
│   │   ├── coze_ws.c
│   │   ├── coze_protocol.c
│   │   └── CMakeLists.txt
│   ├── ui_lvgl/                # LVGL UI Components
│   │   ├── include/
│   │   │   ├── ui_manager.h
│   │   │   ├── ui_idle.h
│   │   │   ├── ui_listening.h
│   │   │   ├── ui_thinking.h
│   │   │   └── ui_speaking.h
│   │   ├── ui_manager.c
│   │   ├── ui_idle.c
│   │   ├── ui_listening.c
│   │   ├── ui_thinking.c
│   │   ├── ui_speaking.c
│   │   ├── ui_assets.c
│   │   └── CMakeLists.txt
│   └── app_core/               # Application Core Logic
│       ├── include/
│       │   ├── app_core.h
│       │   ├── app_wifi.h
│       │   └── app_events.h
│       ├── app_core.c
│       ├── app_wifi.c
│       ├── app_events.c
│       └── CMakeLists.txt
└── README.md
```

## Prerequisites

### Software Requirements

1. **ESP-IDF v5.1+**
   ```bash
   git clone -b v5.1.2 --recursive https://github.com/espressif/esp-idf.git
   cd esp-idf
   ./install.sh esp32s3
   . ./export.sh
   ```

2. **ESP-ADF v2.6+**
   ```bash
   git clone --recursive https://github.com/espressif/esp-adf.git
   export ADF_PATH=$PWD/esp-adf
   ```

3. **LVGL (included via ESP-IDF component registry)**

### Hardware Connections

The Waveshare board has all components integrated. Default pin assignments:

| Function | GPIO |
|----------|------|
| I²S MIC BCLK | GPIO 41 |
| I²S MIC WS | GPIO 42 |
| I²S MIC DATA | GPIO 2 |
| I²S SPK BCLK | GPIO 15 |
| I²S SPK WS | GPIO 16 |
| I²S SPK DATA | GPIO 7 |
| DISPLAY SPI CLK | GPIO 47 |
| DISPLAY SPI MOSI | GPIO 21 |
| DISPLAY CS | GPIO 45 |
| DISPLAY DC | GPIO 40 |
| DISPLAY RST | GPIO 39 |
| TOUCH I²C SDA | GPIO 4 |
| TOUCH I²C SCL | GPIO 5 |
| TOUCH INT | GPIO 6 |
| BOOT BUTTON | GPIO 0 |
| LED | GPIO 38 |

## Configuration

### WiFi Configuration

Edit `main/app_main.c` or use menuconfig:
```c
#define WIFI_SSID "your_wifi_ssid"
#define WIFI_PASS "your_wifi_password"
```

### Coze API Configuration

Edit `components/coze_ws/include/coze_ws.h`:
```c
#define COZE_API_TOKEN "your_coze_api_token"
#define COZE_BOT_ID "your_bot_id"
#define COZE_VOICE_ID "your_voice_id"
```

Get your API credentials from [Coze Platform](https://www.coze.com/).

## Building

```bash
# Set up environment
. $IDF_PATH/export.sh
export ADF_PATH=/path/to/esp-adf

# Configure (optional)
idf.py menuconfig

# Build
idf.py build

# Flash
idf.py -p /dev/ttyUSB0 flash monitor
```

## PlatformIO Support

```ini
[env:waveshare_esp32s3_amoled]
platform = espressif32
board = esp32-s3-devkitc-1
framework = espidf
board_build.mcu = esp32s3
board_build.f_cpu = 240000000L
board_build.flash_mode = qio
board_upload.flash_size = 16MB
board_build.partitions = partitions.csv
build_flags =
    -DBOARD_HAS_PSRAM
    -mfix-esp32-psram-cache-issue
```

## Usage

1. Power on the device
2. Wait for WiFi connection (status shown on display)
3. Tap the screen or press BOOT button to start listening
4. Speak your question
5. Wait for AI response (shown on screen and played through speaker)
6. Conversation continues hands-free with VAD

## State Machine

```
┌─────────┐    tap/button    ┌───────────┐
│  IDLE   │ ───────────────► │ LISTENING │
└─────────┘                  └───────────┘
     ▲                            │
     │                       voice detected
     │                            │
     │                            ▼
┌─────────┐    audio done    ┌───────────┐
│SPEAKING │ ◄─────────────── │ THINKING  │
└─────────┘                  └───────────┘
     │
     │ response complete
     ▼
┌─────────┐
│  IDLE   │ (or continue conversation)
└─────────┘
```

## Audio Pipeline

### Recording Pipeline
```
Dual Mic Array (I²S RX)
        │
        ▼
   AEC Filter (Echo Cancellation)
        │
        ▼
   NS Filter (Noise Suppression)
        │
        ▼
   VAD (Voice Activity Detection)
        │
        ▼
   Opus Encoder
        │
        ▼
   WebSocket TX → Coze Server
```

### Playback Pipeline
```
   WebSocket RX ← Coze Server
        │
        ▼
   Audio Decoder (PCM/MP3)
        │
        ▼
   Volume Control
        │
        ▼
   I²S TX → Speaker
```

## Troubleshooting

### No Audio Input
- Check microphone connections
- Verify I²S configuration in `bsp_i2s.c`
- Check `sdkconfig` for correct I²S settings

### Display Not Working
- Verify SPI connections
- Check display initialization sequence in `bsp_display.c`
- Ensure LVGL tick timer is running

### WebSocket Connection Failed
- Verify WiFi connection
- Check Coze API credentials
- Ensure correct WebSocket URL

### Echo During Playback
- Adjust AEC parameters in `audio_pipeline.c`
- Reduce speaker volume
- Check microphone placement

## License

MIT License - See LICENSE file for details.

## Acknowledgments

- [ESP-IDF](https://github.com/espressif/esp-idf)
- [ESP-ADF](https://github.com/espressif/esp-adf)
- [LVGL](https://github.com/lvgl/lvgl)
- [Coze AI Platform](https://www.coze.com/)
