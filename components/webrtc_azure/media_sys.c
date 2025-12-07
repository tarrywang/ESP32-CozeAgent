/* Media System for WebRTC Azure
 *
 * Provides audio capture and playback for WebRTC communication.
 * Adapted for the ESP32-S3-Touch-AMOLED-1.75 BSP audio system.
 */

#include "esp_log.h"
#include "esp_err.h"
#include "esp_capture_path_simple.h"
#include "esp_capture_audio_enc.h"
#include "av_render.h"
#include "webrtc_azure_settings.h"
#include "media_lib_os.h"
#include "esp_timer.h"
#include "av_render_default.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_enc_default.h"
#include "esp_capture_defaults.h"
#include "esp_webrtc.h"

// codec_board includes for TDM mode audio initialization (required for AEC)
#include "codec_init.h"
#include "codec_board.h"

#define RET_ON_NULL(ptr, v) do {                                \
    if (ptr == NULL) {                                          \
        ESP_LOGE(TAG, "Memory allocate fail on %d", __LINE__);  \
        return v;                                               \
    }                                                           \
} while (0)

#define TAG "MEDIA_SYS"

typedef struct {
    esp_capture_path_handle_t   capture_handle;
    esp_capture_aenc_if_t      *aud_enc;
    esp_capture_audio_src_if_t *aud_src;
    esp_capture_path_if_t      *path_if;
} capture_system_t;

typedef struct {
    audio_render_handle_t audio_render;
    av_render_handle_t    player;
} player_system_t;

static capture_system_t capture_sys;
static player_system_t  player_sys;
static bool s_media_initialized = false;
static bool s_audio_board_initialized = false;

/**
 * @brief Initialize audio board with TDM mode for AEC support
 *
 * This function uses codec_board to initialize audio hardware in TDM mode,
 * which provides 4-channel I2S data required for AEC (Acoustic Echo Cancellation).
 * Channel 0: MIC input (user voice)
 * Channel 1: Reference signal (speaker output for echo cancellation)
 */
void init_audio_board(void)
{
    if (s_audio_board_initialized) {
        ESP_LOGW(TAG, "Audio board already initialized");
        return;
    }

    ESP_LOGI(TAG, "Initializing audio board with TDM mode for AEC");
    ESP_LOGI(TAG, "Board type: %s", TEST_BOARD_NAME);

    // Set board type for codec_board configuration
    set_codec_board_type(TEST_BOARD_NAME);

    // Configure codec with TDM mode for 4-channel audio (required for AEC)
    codec_init_cfg_t cfg = {
#if CONFIG_IDF_TARGET_ESP32S3
        .in_mode = CODEC_I2S_MODE_TDM,   // TDM mode for 4-channel input
        .in_use_tdm = true,               // Enable TDM for ES7210
#endif
        .reuse_dev = false
    };

    // Initialize codec hardware
    init_codec(&cfg);

    s_audio_board_initialized = true;
    ESP_LOGI(TAG, "Audio board initialized with TDM mode");
}

static int build_capture_system(void)
{
    ESP_LOGI(TAG, "Building capture system...");

    capture_sys.aud_enc = esp_capture_new_audio_encoder();
    RET_ON_NULL(capture_sys.aud_enc, -1);

    // Configure audio source with AEC (Acoustic Echo Cancellation)
    // For ESP32-S3 with ES7210, TDM mode uses 4 channels where second channel is reference data
    esp_capture_audio_aec_src_cfg_t codec_cfg = {
        .record_handle = get_record_handle(),
#if CONFIG_IDF_TARGET_ESP32S3
        .channel = 4,
        .channel_mask = 1 | 2,
#endif
    };

    capture_sys.aud_src = esp_capture_new_audio_aec_src(&codec_cfg);
    RET_ON_NULL(capture_sys.aud_src, -1);
    ESP_LOGI(TAG, "Audio AEC source created with record_handle=%p", codec_cfg.record_handle);

    esp_capture_simple_path_cfg_t simple_cfg = {
        .aenc = capture_sys.aud_enc,
    };
    capture_sys.path_if = esp_capture_build_simple_path(&simple_cfg);
    RET_ON_NULL(capture_sys.path_if, -1);

    // Create capture system
    esp_capture_cfg_t cfg = {
        .sync_mode = ESP_CAPTURE_SYNC_MODE_AUDIO,
        .audio_src = capture_sys.aud_src,
        .capture_path = capture_sys.path_if,
    };
    esp_capture_open(&cfg, &capture_sys.capture_handle);

    ESP_LOGI(TAG, "Capture system built successfully");
    return 0;
}

static int build_player_system(void)
{
    ESP_LOGI(TAG, "Building player system...");

    i2s_render_cfg_t i2s_cfg = {
        .play_handle = get_playback_handle(),
    };

    player_sys.audio_render = av_render_alloc_i2s_render(&i2s_cfg);
    if (player_sys.audio_render == NULL) {
        ESP_LOGE(TAG, "Failed to create audio render");
        return -1;
    }

    // Set volume
    esp_codec_dev_set_out_vol(i2s_cfg.play_handle, DEFAULT_PLAYBACK_VOL);

    av_render_cfg_t render_cfg = {
        .audio_render = player_sys.audio_render,
        .audio_raw_fifo_size = 8 * 4096,
        .audio_render_fifo_size = 100 * 1024,
        .allow_drop_data = false,
    };

    player_sys.player = av_render_open(&render_cfg);
    if (player_sys.player == NULL) {
        ESP_LOGE(TAG, "Failed to create player");
        return -1;
    }

    // Configure audio output format (2 channels for AEC reference)
    av_render_audio_frame_info_t aud_info = {
        .sample_rate = 16000,
        .channel = 2,
        .bits_per_sample = 16,
    };
    av_render_set_fixed_frame_info(player_sys.player, &aud_info);

    ESP_LOGI(TAG, "Player system built successfully");
    return 0;
}

int media_sys_buildup(void)
{
    if (s_media_initialized) {
        ESP_LOGW(TAG, "Media system already initialized");
        return 0;
    }

    ESP_LOGI(TAG, "Building media system...");

    // Register default audio encoder
    esp_audio_enc_register_default();

    // Register default audio decoder
    esp_audio_dec_register_default();

    // Build capture system
    if (build_capture_system() != 0) {
        ESP_LOGE(TAG, "Failed to build capture system");
        return -1;
    }

    // Build player system
    if (build_player_system() != 0) {
        ESP_LOGE(TAG, "Failed to build player system");
        return -1;
    }

    s_media_initialized = true;
    ESP_LOGI(TAG, "Media system built successfully");
    return 0;
}

int media_sys_get_provider(esp_webrtc_media_provider_t *provide)
{
    if (!s_media_initialized) {
        ESP_LOGE(TAG, "Media system not initialized");
        return -1;
    }

    provide->capture = capture_sys.capture_handle;
    provide->player = player_sys.player;

    ESP_LOGI(TAG, "Media provider: capture=%p, player=%p",
             provide->capture, provide->player);
    return 0;
}

int test_capture_to_player(void)
{
    ESP_LOGI(TAG, "Starting capture to player test...");

    esp_capture_sink_cfg_t sink_cfg = {
        .audio_info = {
            .codec = ESP_CAPTURE_CODEC_TYPE_OPUS,
            .sample_rate = 16000,
            .channel = 1,
            .bits_per_sample = 16,
        },
    };

    esp_capture_path_handle_t capture_path = NULL;
    esp_capture_setup_path(capture_sys.capture_handle, ESP_CAPTURE_PATH_PRIMARY, &sink_cfg, &capture_path);
    esp_capture_enable_path(capture_path, ESP_CAPTURE_RUN_TYPE_ALWAYS);

    av_render_audio_info_t render_aud_info = {
        .codec = AV_RENDER_AUDIO_CODEC_OPUS,
        .sample_rate = 16000,
        .channel = 1,
    };
    av_render_add_audio_stream(player_sys.player, &render_aud_info);

    uint32_t start_time = (uint32_t)(esp_timer_get_time() / 1000);
    esp_capture_start(capture_sys.capture_handle);

    while ((uint32_t)(esp_timer_get_time() / 1000) < start_time + 20000) {
        media_lib_thread_sleep(30);
        esp_capture_stream_frame_t frame = {
            .stream_type = ESP_CAPTURE_STREAM_TYPE_AUDIO,
        };
        while (esp_capture_acquire_path_frame(capture_path, &frame, true) == ESP_CAPTURE_ERR_OK) {
            av_render_audio_data_t audio_data = {
                .data = frame.data,
                .size = frame.size,
                .pts = frame.pts,
            };
            av_render_add_audio_data(player_sys.player, &audio_data);
            esp_capture_release_path_frame(capture_path, &frame);
        }
    }

    esp_capture_stop(capture_sys.capture_handle);
    av_render_reset(player_sys.player);

    ESP_LOGI(TAG, "Capture to player test completed");
    return 0;
}
