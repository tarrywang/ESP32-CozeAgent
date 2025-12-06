# Display and UI Architecture Exploration - Summary Report

**Project**: ESP32-S3 Coze Voice Agent
**Date**: 2025-12-03
**Focus**: LVGL Display Implementation and UI Rendering Architecture

---

## 1. EXECUTIVE SUMMARY

The ESP32-S3 Coze Voice Agent uses **LVGL (Light and Versatile Graphics Library) v9.x** as its primary UI framework. The project implements a **multi-page state-based UI system** with animated transitions between distinct application states: IDLE, LISTENING, THINKING, SPEAKING, ERROR, and BOOT.

**Current Status**: The display is **DISABLED in app_main.c** (lines 325-328) due to hardware issues, but the LVGL infrastructure is fully functional and ready to be re-enabled.

---

## 2. DISPLAY ARCHITECTURE OVERVIEW

### 2.1 Hardware Configuration
- **Device**: Waveshare ESP32-S3 Touch AMOLED 1.75" (466x466 circular display)
- **Display Type**: AMOLED (optimized for dark themes)
- **Touch Support**: Capacitive touch screen
- **BSP Used**: Official Waveshare BSP (`waveshare__esp32_s3_touch_amoled_1_75`)

### 2.2 UI Framework Stack
```
┌─────────────────────────────────┐
│   Application Layer             │
│   (app_core, app_events)        │
└──────────────┬──────────────────┘
               │
┌──────────────▼──────────────────┐
│   UI Manager (ui_manager.c)     │
│   - Page management             │
│   - State transitions           │
│   - Event dispatching           │
│   - Thread-safe LVGL access     │
└──────────────┬──────────────────┘
               │
┌──────────────▼──────────────────┐
│   Page Implementations          │
│   - ui_idle.c                   │
│   - ui_listening.c              │
│   - ui_thinking.c               │
│   - ui_speaking.c               │
│   - ui_error_page               │
│   - ui_boot_screen              │
└──────────────┬──────────────────┘
               │
┌──────────────▼──────────────────┐
│   LVGL (v9.x)                   │
│   - Rendering engine            │
│   - Animation system            │
│   - Touch input handling        │
│   - Widget library              │
└──────────────┬──────────────────┘
               │
┌──────────────▼──────────────────┐
│   esp_lvgl_port                 │
│   - LVGL-ESP32 integration      │
│   - Display driver setup        │
│   - Touch driver setup          │
│   - Timer callbacks             │
└──────────────┬──────────────────┘
               │
┌──────────────▼──────────────────┐
│   Waveshare BSP                 │
│   - LCD panel control           │
│   - Touch IC (CST816S)          │
│   - I2C communication           │
│   - GPIO management             │
└─────────────────────────────────┘
```

---

## 3. UI MANAGER (ui_manager.c/h)

### 3.1 Core Responsibilities
Located at: `/components/ui_lvgl/ui_manager.{c,h}`

**Functions**:
- Initialize and deinitialize LVGL display
- Manage UI page lifecycle (create, show, hide, destroy)
- Handle page transitions with animations
- Provide thread-safe LVGL access via mutex
- Manage status bar (WiFi, battery, time indicators)
- Display toast notifications
- Update transcripts and audio levels
- Handle touch gestures and UI events

### 3.2 Configuration
```c
// Display dimensions (466x466 circular AMOLED)
#define UI_SCREEN_WIDTH         466
#define UI_SCREEN_HEIGHT        466

// Color theme (dark AMOLED optimized)
#define UI_COLOR_BG             0x000000    // Pure black (saves AMOLED power)
#define UI_COLOR_PRIMARY        0x4A90D9    // Blue accent
#define UI_COLOR_LISTENING      0x3498DB    // Blue
#define UI_COLOR_THINKING       0xF39C12    // Orange
#define UI_COLOR_SPEAKING       0x2ECC71    // Green

// Animation timings
#define UI_ANIM_DURATION_FAST   200ms
#define UI_ANIM_DURATION_NORMAL 400ms
#define UI_ANIM_DURATION_SLOW   800ms
```

### 3.3 Page Enumeration
```c
typedef enum {
    UI_PAGE_BOOT = 0,       // Boot/splash screen with spinner
    UI_PAGE_IDLE,           // Idle state - ready for interaction (pulsing icon)
    UI_PAGE_LISTENING,      // Listening for user speech (wave bars + transcript)
    UI_PAGE_THINKING,       // Processing/thinking (spinner + animated dots)
    UI_PAGE_SPEAKING,       // Playing AI response (wave bars + scrolling text)
    UI_PAGE_ERROR,          // Error state with message
    UI_PAGE_SETTINGS,       // Settings page (not yet implemented)
    UI_PAGE_MAX
} ui_page_t;
```

### 3.4 Key Data Structures
- **s_screen**: Main LVGL screen object (root)
- **s_pages[UI_PAGE_MAX]**: Array of page objects (hidden/shown based on state)
- **s_status_bar**: Top status bar containing WiFi, time, battery
- **s_ui_task**: FreeRTOS task running LVGL timer handler
- **s_ui_mutex**: Recursive mutex protecting LVGL access

### 3.5 Thread Safety Model
```c
// LVGL access is protected by recursive mutex
bool ui_manager_lock(int timeout_ms)
{
    return bsp_display_lock((uint32_t)timeout_ms);
}

void ui_manager_unlock(void)
{
    bsp_display_unlock();
}
```

The UI task runs on **Core 0** with priority 5:
```c
xTaskCreatePinnedToCore(
    ui_task,        // Handler that calls lv_timer_handler()
    "ui_task",
    4096,           // Stack size
    NULL,
    5,              // Priority
    &s_ui_task,
    0               // Core 0
);
```

---

## 4. PAGE STRUCTURE & IMPLEMENTATION

### 4.1 Common Pattern (Each Page)

All pages follow a consistent structure:
```c
// Per-page header (ui_xxx.h)
#include "ui_manager.h"

lv_obj_t *ui_xxx_create(lv_obj_t *parent);
void ui_xxx_enter(void);
void ui_xxx_exit(void);
void ui_xxx_destroy(void);

// Per-page implementation (ui_xxx.c)
static lv_obj_t *s_page;           // Page container
static lv_anim_t s_anim;           // Animation structure
static [other local widgets];

// Create: Build all LVGL objects
// Enter: Start animations, reset state
// Exit: Stop animations, pause activity
// Destroy: Cleanup resources
```

### 4.2 Page Implementations

#### **IDLE Page (ui_idle.c)**
- **Visual**: Microphone icon + pulsing circle background
- **Animation**: 2-second pulse loop (circle opacity 255→0, size 100→150px)
- **Purpose**: Ready state, waiting for user interaction
- **Elements**:
  - Pulsing circle (behind icon)
  - Microphone symbol (LV_SYMBOL_AUDIO)
  - "Tap to speak" hint text (dimmed)

#### **LISTENING Page (ui_listening.c)**
- **Visual**: "Listening..." title + 5 animated wave bars + transcript
- **Animation**: Wave bars animate based on audio level input
- **Purpose**: Capturing user voice input
- **Elements**:
  - Title label ("Listening...")
  - 5 vertical bars (wave visualization)
  - Live transcript label (updates via `ui_listening_update_text()`)
  - Audio level tracking (0-100%)

**Wave Bar Algorithm**:
```c
for (int i = 0; i < 5; i++) {
    int height = base_height[i] + (audio_level * base_height[i] / 100);
    height += animated_offset;  // Creates wave motion
    lv_obj_set_height(bar[i], height);
}
```

#### **THINKING Page (ui_thinking.c)**
- **Visual**: "Thinking" title + spinner + 3 animated dots
- **Animation**: Staggered dot opacity animation (200ms delay between dots)
- **Purpose**: Processing/analyzing user input
- **Elements**:
  - Title label ("Thinking")
  - Spinner (1000ms period, 60° arc)
  - 3 dots with staggered fade-in/fade-out animation

**Dot Animation**:
```c
for (int i = 0; i < 3; i++) {
    lv_anim_set_time(600ms);          // 600ms fade cycle
    lv_anim_set_delay(i * 200ms);     // Staggered start
    lv_anim_set_repeat_count(INFINITE);
    lv_anim_set_playback_time(600ms); // Same duration both directions
}
```

#### **SPEAKING Page (ui_speaking.c)**
- **Visual**: "Speaking" title + 5 wave bars + scrolling AI response text
- **Animation**: Wave bars respond to speaker output audio level
- **Purpose**: Playing AI voice response
- **Elements**:
  - Title label ("Speaking")
  - 5 wave bars (different heights than listening)
  - Scrollable transcript container (2048 char buffer)
  - Live text append capability

#### **BOOT Page (create_boot_page in ui_manager.c)**
- **Visual**: Logo + spinner + status message
- **Animation**: Rotating spinner
- **Purpose**: System initialization splash screen
- **Elements**:
  - Title ("Coze Voice")
  - Subtitle ("AI Assistant")
  - Spinner (1000ms period)
  - Status label (updates via messages)

#### **ERROR Page (create_error_page in ui_manager.c)**
- **Visual**: Warning icon + error message + retry hint
- **Purpose**: Display error conditions
- **Elements**:
  - Warning icon (LV_SYMBOL_WARNING)
  - Dynamic error message label
  - "Tap to retry" hint

### 4.3 Status Bar (Common to All Pages)
```c
static void create_status_bar(lv_obj_t *parent)
{
    // 40px bar at top with:
    // [WiFi Icon] [Time] [Battery Icon]
    //  (left)     (center)  (right)
}
```

Updates handled by:
- `ui_manager_update_wifi_status(connected, rssi)` → colors WiFi icon
- `ui_manager_update_battery(level, charging)` → updates battery icon
- Time label would be updated by a system timer (currently static "12:00")

---

## 5. PAGE TRANSITION SYSTEM

### 5.1 Transition Flow
```c
esp_err_t ui_manager_set_page(ui_page_t page)
{
    // 1. Check validity
    if (!s_initialized || page >= UI_PAGE_MAX) return ESP_ERR_INVALID_ARG;
    
    // 2. Acquire lock (thread-safe)
    if (!ui_manager_lock(1000ms)) return ESP_ERR_TIMEOUT;
    
    // 3. Exit current page (cleanup, stop animations)
    switch (s_current_page) {
        case UI_PAGE_IDLE: ui_idle_exit(); break;
        case UI_PAGE_LISTENING: ui_listening_exit(); break;
        // ... etc
    }
    
    // 4. Hide current page
    lv_obj_add_flag(s_pages[s_current_page], LV_OBJ_FLAG_HIDDEN);
    
    // 5. Show new page
    lv_obj_clear_flag(s_pages[page], LV_OBJ_FLAG_HIDDEN);
    
    // 6. Enter new page (start animations, init state)
    switch (page) {
        case UI_PAGE_IDLE: ui_idle_enter(); break;
        case UI_PAGE_LISTENING: ui_listening_enter(); break;
        // ... etc
    }
    
    // 7. Update state and unlock
    s_current_page = page;
    ui_manager_unlock();
    return ESP_OK;
}
```

### 5.2 Animation Types Used

**1. Opacity Fade** (page-level)
```c
static void page_opa_anim_cb(void *var, int32_t value)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)value, 0);
}
// Creates fade in/out when transitioning
```

**2. Pulse (IDLE page)**
```c
lv_anim_set_values(255, 0);           // opacity
lv_anim_set_time(2000);               // 2 second cycle
lv_anim_set_repeat_count(INFINITE);   // Loop forever
// Combined with size change: 100px → 150px → 100px
```

**3. Wave Animation** (LISTENING/SPEAKING pages)
- No formal LVGL animation
- Updated directly via `ui_listening_update_level()` called from app_core
- Bars height calculated based on real-time audio level

**4. Staggered Dot Animation** (THINKING page)
```c
for (int i = 0; i < 3; i++) {
    lv_anim_set_delay(i * 200);   // Each dot delays by 200ms
    lv_anim_set_playback_time(same as forward);  // Smooth return
}
// Creates "bouncing" effect as dots fade in/out sequentially
```

---

## 6. EXISTING DISPLAY PATTERNS

### 6.1 Dynamic Content Update Patterns

**Live Transcript Update**:
```c
void ui_manager_update_transcript(const char *text, bool is_user)
{
    if (!ui_manager_lock(50)) return;
    
    if (is_user && s_current_page == UI_PAGE_LISTENING) {
        ui_listening_update_text(text);  // Update during listening
    } else if (!is_user && s_current_page == UI_PAGE_SPEAKING) {
        ui_speaking_append_text(text);   // Append during speaking
    }
    
    ui_manager_unlock();
}
```

**Audio Level Indicator**:
```c
void ui_manager_update_audio_level(uint8_t level)  // 0-100
{
    if (!ui_manager_lock(10)) return;
    
    if (s_current_page == UI_PAGE_LISTENING) {
        ui_listening_update_level(level);   // Update wave bars
    } else if (s_current_page == UI_PAGE_SPEAKING) {
        ui_speaking_update_level(level);    // Update wave bars
    }
    
    ui_manager_unlock();
}
```

**Status Indicators**:
```c
void ui_manager_update_wifi_status(bool connected, int rssi);
void ui_manager_update_battery(uint8_t level, bool charging);
void ui_manager_show_status(const char *message, bool success);
void ui_manager_show_error(const char *message);
```

### 6.2 Toast Notification System
```c
static void show_toast(const char *message, uint32_t duration_ms)
{
    if (ui_manager_lock(100)) {
        lv_label_set_text(s_toast_label, message);
        lv_obj_clear_flag(s_toast_label, LV_OBJ_FLAG_HIDDEN);
        ui_manager_unlock();
        
        // Auto-hide after duration using esp_timer
        esp_timer_stop(s_toast_timer);
        esp_timer_start_once(s_toast_timer, duration_ms * 1000);
    }
}
```

### 6.3 Event Callback System
```c
// Applications register a callback to receive UI events
esp_err_t ui_manager_register_callback(ui_event_callback_t callback, void *user_data)
{
    s_event_callback = callback;
    s_callback_user_data = user_data;
    return ESP_OK;
}

// Touch events detected via LVGL and forwarded
typedef enum {
    UI_EVENT_TAP,           // Single tap
    UI_EVENT_DOUBLE_TAP,    // Double tap
    UI_EVENT_LONG_PRESS,    // Long press
    UI_EVENT_SWIPE_UP,      // Swipe directions
    UI_EVENT_SWIPE_DOWN,
    UI_EVENT_SWIPE_LEFT,
    UI_EVENT_SWIPE_RIGHT,
    UI_EVENT_BUTTON,        // Physical button press
} ui_event_t;
```

---

## 7. DISPLAY INITIALIZATION FLOW (Currently Disabled)

In `app_main.c` (lines 325-328):
```c
// ==== Phase 3: Display + LVGL Initialization ====
// DISABLED: LCD hardware issue - focusing on voice interaction via button/serial
ESP_LOGW(TAG, "Display initialization SKIPPED (hardware issue)");
ESP_LOGW(TAG, "Using BOOT button + serial console for interaction");
s_lv_display = NULL;
s_lv_indev = NULL;
```

**Original initialization would be** (from BSP pattern):
```c
// Initialize BSP display
bsp_display_cfg_t disp_cfg = BSP_DISPLAY_V_FLIP | BSP_DISPLAY_H_FLIP;
esp_err_t ret = bsp_display_new(&disp_cfg, &s_lv_display, &s_lv_indev);

// Set display as active
lv_display_set_default(s_lv_display);

// Start UI manager
ui_manager_init();
ui_manager_start_task();
```

---

## 8. ANIMATION & RENDERING SYSTEM

### 8.1 LVGL Timer Handler
```c
// Running in UI task (Core 0, ~100 FPS)
static void ui_task(void *pvParameters)
{
    while (s_task_running) {
        if (ui_manager_lock(50)) {
            lv_timer_handler();      // Process animations and redraws
            ui_manager_unlock();
        }
        vTaskDelay(10ms);  // ~100 FPS max
    }
}
```

### 8.2 Animation Features
- **LVGL 9.x native animations** (opacity, position, size, color)
- **Custom animation callbacks** for specialized effects (pulse, wave, dots)
- **Repeat patterns**: Infinite loops for continuous feedback
- **Staggered timing**: Delays between related animations
- **Playback timing**: Smooth return animations with same duration

### 8.3 Rendering Pipeline
```
Application sends event
    ↓
app_core processes state change
    ↓
ui_manager_set_page() called
    ↓
LVGL objects hidden/shown
    ↓
Page enter() starts animations
    ↓
UI task calls lv_timer_handler() every 10ms
    ↓
LVGL processes animations (~100 FPS)
    ↓
Framebuffer updated via display driver
    ↓
Changes appear on AMOLED screen
```

---

## 9. COMPONENT DEPENDENCIES

**CMakeLists.txt** requires:
```cmake
idf_component_register(
    SRCS
        "ui_manager.c"
        "ui_idle.c"
        "ui_listening.c"
        "ui_thinking.c"
        "ui_speaking.c"
    REQUIRES
        lvgl                                    # Graphics library
        freertos                                # RTOS kernel
        esp_timer                               # Timer for toasts
        esp_lvgl_port                           # LVGL-ESP32 integration
        waveshare__esp32_s3_touch_amoled_1_75   # BSP
)
```

**Key External Functions** (from app_main):
```c
lv_display_t *app_get_display(void);    // Get LVGL display handle
lv_indev_t *app_get_input_dev(void);    // Get touch input device
```

---

## 10. DISPLAY-RELATED CONFIGURATION

### 10.1 Screen Dimensions & Aspect Ratio
- **Resolution**: 466x466 pixels (circular AMOLED)
- **Aspect Ratio**: 1:1 square (note: physical display is circular)
- **Effective UI Area**: Center-focused for circular display

### 10.2 Color Scheme
```
Background:    #000000  (Pure black - saves AMOLED power)
Primary:       #4A90D9  (Bright blue)
Listening:     #3498DB  (Medium blue)
Thinking:      #F39C12  (Orange)
Speaking:      #2ECC71  (Green)
Error:         #E74C3C  (Red)
Text:          #FFFFFF  (White)
Text-Dim:      #888888  (Gray)
```

### 10.3 Font Usage
- `lv_font_montserrat_14` - Small text
- `lv_font_montserrat_16` - Normal text
- `lv_font_montserrat_18` - Larger labels
- `lv_font_montserrat_20` - Icons (WiFi, battery)
- `lv_font_montserrat_32` - Titles (boot screen)

---

## 11. NO CAROUSEL/PAGE-SWIPE PATTERNS FOUND

**Important Finding**: The codebase does **NOT** implement:
- Horizontal scrolling/swiping between pages
- Carousel/slide show patterns
- Multi-page navigation with swipe gestures
- Page indicator dots for sequential browsing

**Why**: The page system is **state-based**, not sequential:
- Pages are tied to application state (IDLE → LISTENING → THINKING → SPEAKING)
- Transitions are programmatic (`ui_manager_set_page()`), not gesture-driven
- No implicit "next page" concept—always return to application state

**Touch Events** that ARE captured:
```c
UI_EVENT_TAP          // Generic tap
UI_EVENT_SWIPE_*      // Directions registered but not used for page nav
UI_EVENT_LONG_PRESS   // Long press
```

---

## 12. KEY INSIGHTS & PATTERNS

### 12.1 Architecture Strengths
1. **Clean separation**: UI logic separate from application logic
2. **Thread-safe**: Mutex protects all LVGL access
3. **State-driven**: Pages reflect application state, not user navigation
4. **Modular pages**: Each page is independent, easy to modify
5. **Animation framework**: Common patterns for entry/exit animations
6. **Responsive updates**: Audio level and transcript updates are real-time

### 12.2 Display Update Mechanisms
1. **Page Transitions**: Full page swap with animations
2. **Live Updates**: Non-blocking transcripts and audio levels (within page)
3. **Status Bar**: Always-visible indicators (WiFi, battery, time)
4. **Toast Notifications**: Auto-dismissing messages
5. **Animation Loop**: LVGL timer handler processes all animations

### 12.3 Currently Disabled
- Display hardware (LCD initialization disabled)
- UI Manager initialization
- UI Task (LVGL handler)
- Touch input processing
- All visual feedback

However: **The entire UI infrastructure is functional code-wise**, just not compiled/running due to hardware issue.

---

## 13. SUMMARY TABLE

| Aspect | Details |
|--------|---------|
| **Framework** | LVGL 9.x |
| **Resolution** | 466x466 (circular AMOLED) |
| **Color Model** | RGB888 / 16-bit RGB |
| **Pages** | 7 (BOOT, IDLE, LISTENING, THINKING, SPEAKING, ERROR, SETTINGS) |
| **Animation System** | Native LVGL + custom callbacks |
| **Touch Events** | 7 event types (tap, swipe, long press, etc.) |
| **Thread Model** | Single UI task (Core 0) + application tasks (Core 1) |
| **Mutex Protection** | Recursive mutex for thread-safe LVGL access |
| **Status Bar** | WiFi, Time, Battery (always visible) |
| **Toast System** | Auto-dismissing notifications with timer |
| **Dynamic Updates** | Transcript appending, audio level visualization |
| **Page Carousel** | NO - state-based system, not gesture-driven |
| **Current Status** | Disabled (hardware issue), code intact and functional |

---

## 14. FILES INVOLVED

| File | Purpose |
|------|---------|
| `components/ui_lvgl/ui_manager.h` | Public API and configuration |
| `components/ui_lvgl/ui_manager.c` | Core UI manager implementation |
| `components/ui_lvgl/ui_idle.c` | Idle page (pulsing icon) |
| `components/ui_lvgl/ui_listening.c` | Listening page (wave bars + transcript) |
| `components/ui_lvgl/ui_thinking.c` | Thinking page (spinner + dots) |
| `components/ui_lvgl/ui_speaking.c` | Speaking page (wave bars + text) |
| `main/app_main.c` | Display initialization (currently disabled) |
| `components/ui_lvgl/CMakeLists.txt` | Component configuration |

---

## CONCLUSION

The ESP32-S3 Coze Voice Agent implements a **professional, well-architected LVGL-based UI system** optimized for circular AMOLED displays. The system features:

- **Multi-state page system** with smooth animated transitions
- **Real-time content updates** (transcripts, audio levels)
- **Thread-safe rendering** with mutex protection
- **Rich animations** (pulse, wave visualization, staggered dots)
- **Status indicators** and toast notifications
- **Touch event handling** for user interaction

While currently disabled due to hardware issues, the code is production-ready and demonstrates best practices for embedded LVGL applications on ESP32-S3.

No carousel or horizontal page-swiping patterns exist—the architecture is deliberately state-centric, reflecting the voice assistant's operational model.
