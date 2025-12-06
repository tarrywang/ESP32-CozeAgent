/**
 * @file ui_info_carousel.h
 * @brief Information carousel component for IDLE state
 *
 * Provides a 6-page tileview carousel displaying:
 * - WiFi signal strength
 * - Battery status
 * - Memory usage
 * - Temperature
 * - GPS info (optional)
 * - Date/Time
 *
 * Features:
 * - Auto-rotation every 5 seconds
 * - Swipe gesture support
 * - Animated page transitions
 */

#ifndef UI_INFO_CAROUSEL_H
#define UI_INFO_CAROUSEL_H

#include "lvgl.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Carousel page identifiers
 */
typedef enum {
    CAROUSEL_PAGE_WIFI = 0,     // WiFi signal info
    CAROUSEL_PAGE_BATTERY,      // Battery status
    CAROUSEL_PAGE_MEMORY,       // Memory usage
    CAROUSEL_PAGE_TEMPERATURE,  // Temperature
    CAROUSEL_PAGE_GPS,          // GPS info
    CAROUSEL_PAGE_DATETIME,     // Date and time
    CAROUSEL_PAGE_MAX
} carousel_page_t;

/**
 * @brief Initialize the info carousel
 *
 * Creates the tileview and all 6 info tiles.
 * Must be called after LVGL is initialized.
 *
 * @param parent Parent object (typically IDLE page container)
 * @return ESP_OK on success
 */
esp_err_t ui_info_carousel_init(lv_obj_t *parent);

/**
 * @brief Deinitialize and destroy the carousel
 *
 * @return ESP_OK on success
 */
esp_err_t ui_info_carousel_deinit(void);

/**
 * @brief Show the carousel
 *
 * Makes the carousel visible and starts auto-rotation.
 */
void ui_info_carousel_show(void);

/**
 * @brief Hide the carousel
 *
 * Hides the carousel and stops auto-rotation.
 */
void ui_info_carousel_hide(void);

/**
 * @brief Check if carousel is visible
 *
 * @return true if visible
 */
bool ui_info_carousel_is_visible(void);

/**
 * @brief Update carousel data from system_info
 *
 * Should be called periodically (e.g., every 1 second) to
 * refresh displayed information.
 */
void ui_info_carousel_update(void);

/**
 * @brief Start auto-rotation timer
 *
 * Pages will automatically advance every 5 seconds.
 */
void ui_info_carousel_start_auto_rotate(void);

/**
 * @brief Stop auto-rotation timer
 */
void ui_info_carousel_stop_auto_rotate(void);

/**
 * @brief Go to a specific page
 *
 * @param page Page to display
 * @param animate Use animation transition
 */
void ui_info_carousel_goto_page(carousel_page_t page, bool animate);

/**
 * @brief Go to next page
 *
 * @param animate Use animation transition
 */
void ui_info_carousel_next_page(bool animate);

/**
 * @brief Go to previous page
 *
 * @param animate Use animation transition
 */
void ui_info_carousel_prev_page(bool animate);

/**
 * @brief Get current page
 *
 * @return Current page identifier
 */
carousel_page_t ui_info_carousel_get_page(void);

#ifdef __cplusplus
}
#endif

#endif // UI_INFO_CAROUSEL_H
