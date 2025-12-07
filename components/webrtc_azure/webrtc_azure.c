/* WebRTC Azure Integration
 *
 * Integrates Azure OpenAI Realtime API with WebRTC for real-time voice conversations.
 * Based on webrtc_openai implementation.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_webrtc.h"
#include "media_lib_os.h"
#include "esp_webrtc_defaults.h"
#include "esp_peer_default.h"
#include "webrtc_azure.h"
#include "webrtc_azure_settings.h"
#include <cJSON.h>

#define TAG "WEBRTC_AZURE"

#define ELEMS(a) (sizeof(a) / sizeof(a[0]))

// Forward declarations for signaling and media
extern const esp_peer_signaling_impl_t *esp_signaling_get_openai_signaling(void);
extern int media_sys_buildup(void);
extern int media_sys_get_provider(esp_webrtc_media_provider_t *provide);

// Function Calling structures
typedef struct attribute_t attribute_t;
typedef struct class_t class_t;

typedef enum {
    ATTRIBUTE_TYPE_NONE,
    ATTRIBUTE_TYPE_BOOL,
    ATTRIBUTE_TYPE_INT,
    ATTRIBUTE_TYPE_PARENT,
} attribute_type_t;

struct attribute_t {
    char            *name;
    char            *desc;
    attribute_type_t type;
    union {
        bool         b_state;
        int          i_value;
        attribute_t *attr_list;
    };
    int  attr_num;
    bool required;
    int (*control)(attribute_t *attr);
};

struct class_t {
    char        *name;
    char        *desc;
    attribute_t *attr_list;
    int          attr_num;
    class_t     *next;
};

// Module state
static esp_webrtc_handle_t s_webrtc = NULL;
static class_t *s_classes = NULL;
static bool s_initialized = false;
static bool s_connected = false;
static bool s_data_channel_open = false;
static webrtc_azure_event_cb_t s_event_cb = NULL;
static void *s_user_data = NULL;

// ============================================
// Thread Stack Size Configuration
// ============================================

/**
 * @brief Thread scheduler callback to customize stack sizes
 *
 * Only increases stack size for pc_task (peer connection task),
 * without affecting other modules that use media_lib_thread.
 */
static void webrtc_thread_scheduler(const char *name, media_lib_thread_cfg_t *cfg)
{
    // Thread scheduler configuration matching working webrtc_openai sample
    // Critical for WebRTC stability - insufficient stack causes "Fail to new connection"
    ESP_LOGI(TAG, "Thread scheduler: '%s' (default stack=%d)", name, cfg->stack_size);

    if (strcmp(name, "pc_task") == 0) {
        cfg->stack_size = 25 * 1024;  // 25KB - peer connection (DTLS/SRTP/ICE)
        cfg->priority = 18;
        cfg->core_id = 1;
        ESP_LOGI(TAG, "pc_task: stack=%d, priority=%d, core=%d", cfg->stack_size, cfg->priority, cfg->core_id);
    }
    else if (strcmp(name, "start") == 0) {
        cfg->stack_size = 6 * 1024;  // 6KB - startup task
        ESP_LOGI(TAG, "start: stack=%d", cfg->stack_size);
    }
    else if (strcmp(name, "pc_send") == 0) {
        cfg->stack_size = 4 * 1024;  // 4KB - peer connection send
        cfg->priority = 15;
        cfg->core_id = 1;
        ESP_LOGI(TAG, "pc_send: stack=%d, priority=%d, core=%d", cfg->stack_size, cfg->priority, cfg->core_id);
    }
    else if (strcmp(name, "Adec") == 0 || strcmp(name, "adec") == 0) {
        cfg->stack_size = 40 * 1024;  // 40KB - OPUS decoder (needs large stack!)
        cfg->priority = 10;
        cfg->core_id = 1;
        ESP_LOGI(TAG, "Adec: stack=%d, priority=%d, core=%d", cfg->stack_size, cfg->priority, cfg->core_id);
    }
    else if (strcmp(name, "venc") == 0) {
#if CONFIG_IDF_TARGET_ESP32S3
        cfg->stack_size = 20 * 1024;  // 20KB - video encoder
#endif
        cfg->priority = 10;
        ESP_LOGI(TAG, "venc: stack=%d, priority=%d", cfg->stack_size, cfg->priority);
    }
#ifdef WEBRTC_SUPPORT_OPUS
    else if (strcmp(name, "aenc") == 0) {
        cfg->stack_size = 40 * 1024;  // 40KB - OPUS encoder (needs large stack!)
        cfg->priority = 10;
        ESP_LOGI(TAG, "aenc: stack=%d, priority=%d", cfg->stack_size, cfg->priority);
    }
    else if (strcmp(name, "SrcRead") == 0) {
        cfg->stack_size = 40 * 1024;  // 40KB - audio source read
        cfg->priority = 16;
        cfg->core_id = 0;
        ESP_LOGI(TAG, "SrcRead: stack=%d, priority=%d, core=%d", cfg->stack_size, cfg->priority, cfg->core_id);
    }
    else if (strcmp(name, "buffer_in") == 0) {
        cfg->stack_size = 6 * 1024;  // 6KB - buffer input
        cfg->priority = 10;
        cfg->core_id = 0;
        ESP_LOGI(TAG, "buffer_in: stack=%d, priority=%d, core=%d", cfg->stack_size, cfg->priority, cfg->core_id);
    }
#endif
    // Other threads keep default 4KB stack
}

// ============================================
// Function Calling - Demo controls
// ============================================

static int set_light_on_off(attribute_t *attr)
{
    ESP_LOGI(TAG, "Light set to %s", attr->b_state ? "ON" : "OFF");
    return 0;
}

static int set_light_color_red(attribute_t *attr)
{
    ESP_LOGI(TAG, "Red set to %d", attr->i_value);
    return 0;
}

static int set_light_color_blue(attribute_t *attr)
{
    ESP_LOGI(TAG, "Blue set to %d", attr->i_value);
    return 0;
}

static int set_light_color_green(attribute_t *attr)
{
    ESP_LOGI(TAG, "Green set to %d", attr->i_value);
    return 0;
}

static int set_speaker_volume(attribute_t *attr)
{
    ESP_LOGI(TAG, "Volume set to %d", attr->i_value);
    return 0;
}

static int set_door_state(attribute_t *attr)
{
    ESP_LOGI(TAG, "Door is %s", attr->b_state ? "Opened" : "Closed");
    return 0;
}

static class_t *build_volume_class(void)
{
    class_t *vol = (class_t *)calloc(1, sizeof(class_t));
    if (vol == NULL) {
        return NULL;
    }
    static attribute_t vol_attrs[] = {
        {
            .name = "volume",
            .desc = "Speaker volume range 0-100",
            .type = ATTRIBUTE_TYPE_INT,
            .control = set_speaker_volume,
            .required = true,
        },
    };
    vol->name = "SetVolume";
    vol->desc = "Changes speaker volume";
    vol->attr_list = vol_attrs;
    vol->attr_num = ELEMS(vol_attrs);
    return vol;
}

static class_t *build_door_class(void)
{
    class_t *door = (class_t *)calloc(1, sizeof(class_t));
    if (door == NULL) {
        return NULL;
    }
    static attribute_t door_attrs[] = {
        {
            .name = "open",
            .desc = "Open or close the door",
            .type = ATTRIBUTE_TYPE_BOOL,
            .control = set_door_state,
            .required = true,
        },
    };
    door->name = "OpenDoor";
    door->desc = "Toggle the door state to open or close";
    door->attr_list = door_attrs;
    door->attr_num = ELEMS(door_attrs);
    return door;
}

static class_t *build_light_class(void)
{
    class_t *light = (class_t *)calloc(1, sizeof(class_t));
    if (light == NULL) {
        return NULL;
    }
    static attribute_t light_color[] = {
        {
            .name = "red",
            .desc = "Red value in the range of 0-255",
            .type = ATTRIBUTE_TYPE_INT,
            .control = set_light_color_red,
            .required = true,
        },
        {
            .name = "green",
            .desc = "Green value in the range of 0-255",
            .type = ATTRIBUTE_TYPE_INT,
            .control = set_light_color_green,
            .required = true,
        },
        {
            .name = "blue",
            .desc = "Blue value in the range of 0-255",
            .control = set_light_color_blue,
            .type = ATTRIBUTE_TYPE_INT,
            .required = true,
        },
    };
    static attribute_t light_attrs[] = {
        {
            .name = "LightState",
            .desc = "New light state (true or false is expected)",
            .type = ATTRIBUTE_TYPE_BOOL,
            .control = set_light_on_off,
            .required = true,
        },
        {
            .name = "LightColor",
            .desc = "Set light color of red, green and blue",
            .type = ATTRIBUTE_TYPE_PARENT,
            .attr_list = light_color,
            .attr_num = ELEMS(light_color),
        },
    };
    light->name = "SetLightState";
    light->desc = "Changes the state of the light";
    light->attr_list = light_attrs;
    light->attr_num = ELEMS(light_attrs);
    return light;
}

static void add_class(class_t *cls)
{
    if (s_classes == NULL) {
        s_classes = cls;
    } else {
        class_t *iter = s_classes;
        while (iter->next) {
            iter = iter->next;
        }
        iter->next = cls;
    }
}

static int build_classes(void)
{
    static bool build_once = false;
    if (build_once) {
        return 0;
    }
    add_class(build_light_class());
    add_class(build_volume_class());
    add_class(build_door_class());
    build_once = true;
    return 0;
}

// ============================================
// JSON Helpers for Function Calling
// ============================================

static char *get_attr_type(attribute_type_t type)
{
    if (type == ATTRIBUTE_TYPE_BOOL) {
        return "boolean";
    }
    if (type == ATTRIBUTE_TYPE_INT) {
        return "integer";
    }
    if (type == ATTRIBUTE_TYPE_PARENT) {
        return "object";
    }
    return "";
}

static int add_parent_attribute(cJSON *parent, attribute_t *attr)
{
    cJSON *properties = cJSON_CreateObject();
    cJSON_AddItemToObject(parent, "properties", properties);
    int require_num = 0;
    for (int i = 0; i < attr->attr_num; i++) {
        attribute_t *sub_attr = &attr->attr_list[i];
        cJSON *prop = cJSON_CreateObject();
        cJSON_AddItemToObject(properties, sub_attr->name, prop);
        cJSON_AddStringToObject(prop, "type", get_attr_type(sub_attr->type));
        cJSON_AddStringToObject(prop, "description", sub_attr->desc);
        if (sub_attr->type == ATTRIBUTE_TYPE_PARENT) {
            add_parent_attribute(prop, sub_attr);
        }
        if (sub_attr->required) {
            require_num++;
        }
    }
    if (require_num) {
        cJSON *requires = cJSON_CreateArray();
        for (int i = 0; i < attr->attr_num; i++) {
            attribute_t *sub_attr = &attr->attr_list[i];
            if (sub_attr->required) {
                cJSON_AddItemToArray(requires, cJSON_CreateString(sub_attr->name));
            }
        }
        cJSON_AddItemToObject(parent, "required", requires);
    }
    return 0;
}

static int send_function_desc(void)
{
    if (s_classes == NULL || s_webrtc == NULL) {
        return 0;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "session.update");
    cJSON *session = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "session", session);

    cJSON *modalities = cJSON_CreateArray();
    cJSON_AddItemToArray(modalities, cJSON_CreateString("text"));
    cJSON_AddItemToArray(modalities, cJSON_CreateString("audio"));

    cJSON_AddItemToObject(session, "modalities", modalities);
    cJSON_AddNullToObject(session, "input_audio_transcription");

    // Configure turn_detection with high threshold to reduce false triggers from echo
    // Without AEC (Acoustic Echo Cancellation), the microphone captures speaker output
    // Higher threshold (0.9) makes VAD less sensitive to echo, but still detects loud speech
    // Longer silence_duration (1500ms) prevents brief pauses from ending the turn
    cJSON *turn_detection = cJSON_CreateObject();
    cJSON_AddStringToObject(turn_detection, "type", "server_vad");
    cJSON_AddNumberToObject(turn_detection, "threshold", 0.9);           // High threshold (0.0-1.0)
    cJSON_AddNumberToObject(turn_detection, "prefix_padding_ms", 500);   // 500ms padding before speech
    cJSON_AddNumberToObject(turn_detection, "silence_duration_ms", 1500); // 1.5s silence to end turn
    cJSON_AddItemToObject(session, "turn_detection", turn_detection);
    ESP_LOGI(TAG, "turn_detection configured: threshold=0.9, silence=1500ms (no AEC, high threshold)");

    cJSON *tools = cJSON_CreateArray();
    cJSON_AddItemToObject(session, "tools", tools);

    class_t *iter = s_classes;
    while (iter) {
        cJSON *tool = cJSON_CreateObject();
        cJSON_AddItemToArray(tools, tool);
        cJSON_AddStringToObject(tool, "type", "function");
        cJSON_AddStringToObject(tool, "name", iter->name);
        cJSON_AddStringToObject(tool, "description", iter->desc);
        cJSON *parameters = cJSON_CreateObject();
        cJSON_AddItemToObject(tool, "parameters", parameters);
        cJSON_AddStringToObject(parameters, "type", "object");
        cJSON *properties = cJSON_CreateObject();
        cJSON_AddItemToObject(parameters, "properties", properties);
        int require_num = 0;
        for (int i = 0; i < iter->attr_num; i++) {
            attribute_t *attr = &iter->attr_list[i];
            cJSON *prop = cJSON_CreateObject();
            cJSON_AddItemToObject(properties, attr->name, prop);
            cJSON_AddStringToObject(prop, "type", get_attr_type(attr->type));
            cJSON_AddStringToObject(prop, "description", attr->desc);
            if (attr->type == ATTRIBUTE_TYPE_PARENT) {
                add_parent_attribute(prop, attr);
            }
            if (attr->required) {
                require_num++;
            }
        }
        if (require_num) {
            cJSON *requires = cJSON_CreateArray();
            for (int i = 0; i < iter->attr_num; i++) {
                attribute_t *attr = &iter->attr_list[i];
                if (attr->required) {
                    cJSON_AddItemToArray(requires, cJSON_CreateString(attr->name));
                }
            }
            cJSON_AddItemToObject(parameters, "required", requires);
        }
        iter = iter->next;
    }
    char *json_string = cJSON_Print(root);
    if (json_string) {
        ESP_LOGI(TAG, "Sending function descriptions");
        esp_webrtc_send_custom_data(s_webrtc, ESP_WEBRTC_CUSTOM_DATA_VIA_DATA_CHANNEL,
                                    (uint8_t *)json_string, strlen(json_string));
        free(json_string);
    }
    cJSON_Delete(root);
    return 0;
}

// ============================================
// Function Call Processing
// ============================================

static int match_and_execute(cJSON *cur, attribute_t *attr)
{
    cJSON *attr_value = cJSON_GetObjectItemCaseSensitive(cur, attr->name);
    if (!attr_value) {
        if (attr->required) {
            ESP_LOGW(TAG, "Missing required attribute: %s", attr->name);
        }
        return 0;
    }

    if (attr->type == ATTRIBUTE_TYPE_BOOL && cJSON_IsBool(attr_value)) {
        attr->b_state = cJSON_IsTrue(attr_value);
        if (attr->control) {
            attr->control(attr);
        }
    } else if (attr->type == ATTRIBUTE_TYPE_INT && cJSON_IsNumber(attr_value)) {
        attr->i_value = attr_value->valueint;
        if (attr->control) {
            attr->control(attr);
        }
    } else if (attr->type == ATTRIBUTE_TYPE_PARENT && cJSON_IsObject(attr_value)) {
        for (int j = 0; j < attr->attr_num; j++) {
            attribute_t *sub_attr = &attr->attr_list[j];
            match_and_execute(attr_value, sub_attr);
        }
    } else {
        ESP_LOGW(TAG, "Unhandled attribute type or invalid value for: %s", attr->name);
    }

    return 1;
}

static int process_json(const char *json_data)
{
    cJSON *root = cJSON_Parse(json_data);
    if (!root) {
        ESP_LOGW(TAG, "Error parsing JSON data");
        return -1;
    }

    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsString(type) || strcmp(type->valuestring, "response.function_call_arguments.done") != 0) {
        cJSON_Delete(root);
        return 0;
    }

    char *payload = cJSON_PrintUnformatted(root);
    if (payload) {
        ESP_LOGI(TAG, "Function Call: %s", payload);
        free(payload);
    }

    const cJSON *name = cJSON_GetObjectItemCaseSensitive(root, "name");
    const cJSON *arguments = cJSON_GetObjectItemCaseSensitive(root, "arguments");
    if (!cJSON_IsString(name) || !name->valuestring ||
        !cJSON_IsString(arguments) || !arguments->valuestring) {
        ESP_LOGW(TAG, "Invalid JSON format");
        cJSON_Delete(root);
        return -1;
    }

    // Notify callback about function call
    if (s_event_cb) {
        webrtc_azure_event_t event = {
            .type = WEBRTC_AZURE_EVENT_FUNCTION_CALL,
            .function_call = {
                .name = name->valuestring,
                .arguments = arguments->valuestring,
            },
        };
        s_event_cb(&event, s_user_data);
    }

    cJSON *args_root = cJSON_Parse(arguments->valuestring);
    if (!args_root) {
        ESP_LOGW(TAG, "Error parsing arguments JSON");
        cJSON_Delete(root);
        return -1;
    }

    // Find and execute the corresponding class
    class_t *iter = s_classes;
    while (iter) {
        if (strcmp(iter->name, name->valuestring) == 0) {
            for (int i = 0; i < iter->attr_num; i++) {
                attribute_t *attr = &iter->attr_list[i];
                match_and_execute(args_root, attr);
            }
        }
        iter = iter->next;
    }

    cJSON_Delete(args_root);
    cJSON_Delete(root);
    return 0;
}

// ============================================
// WebRTC Data Handler
// ============================================

static int webrtc_data_handler(esp_webrtc_custom_data_via_t via, uint8_t *data, int size, void *ctx)
{
    // Process function calls
    process_json((const char *)data);

    // Extract and log transcripts
    cJSON *root = cJSON_Parse((const char *)data);
    if (!root) {
        return -1;
    }

    char *payload = cJSON_PrintUnformatted(root);
    if (payload) {
        char *text = strstr(payload, "transcript\":");
        if (text) {
            text += strlen("transcript\":");
            char *start = strchr(text, '"');
            char *end = start ? strchr(start + 1, '"') : NULL;
            if (end) {
                start++;
                int len = (int)(end - start);
                char *transcript = malloc(len + 1);
                if (transcript) {
                    memcpy(transcript, start, len);
                    transcript[len] = '\0';
                    ESP_LOGI(TAG, "Transcript: %s", transcript);

                    // Notify callback
                    if (s_event_cb) {
                        webrtc_azure_event_t event = {
                            .type = WEBRTC_AZURE_EVENT_TRANSCRIPT,
                            .transcript = {
                                .text = transcript,
                            },
                        };
                        s_event_cb(&event, s_user_data);
                    }
                    free(transcript);
                }
            }
        }
        free(payload);
    }
    cJSON_Delete(root);
    return 0;
}

// ============================================
// Send Response to Azure
// ============================================

static int send_response(const char *text)
{
    if (s_webrtc == NULL) {
        ESP_LOGE(TAG, "WebRTC not started yet");
        return -1;
    }
    cJSON *root = cJSON_CreateObject();
    if (root) {
        cJSON_AddStringToObject(root, "type", "response.create");
        cJSON *response = cJSON_CreateObject();
        cJSON *modalities = cJSON_CreateArray();
        cJSON_AddItemToArray(modalities, cJSON_CreateString("text"));
        cJSON_AddItemToArray(modalities, cJSON_CreateString("audio"));
        cJSON_AddItemToObject(response, "modalities", modalities);
        cJSON_AddStringToObject(response, "instructions", text);
        cJSON_AddItemToObject(root, "response", response);
    }
    char *send_text = cJSON_Print(root);
    if (send_text) {
        ESP_LOGI(TAG, "Sending response: %s", send_text);
        esp_webrtc_send_custom_data(s_webrtc, ESP_WEBRTC_CUSTOM_DATA_VIA_DATA_CHANNEL,
                                    (uint8_t *)send_text, strlen(send_text));
        free(send_text);
    }
    cJSON_Delete(root);
    return 0;
}

// ============================================
// WebRTC Event Handler
// ============================================

static int webrtc_event_handler(esp_webrtc_event_t *event, void *ctx)
{
    ESP_LOGI(TAG, "==================== WebRTC Event %d ====================", event->type);

    switch (event->type) {
        case ESP_WEBRTC_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WebRTC connected");
            s_connected = true;
            if (s_event_cb) {
                webrtc_azure_event_t evt = {.type = WEBRTC_AZURE_EVENT_CONNECTED};
                s_event_cb(&evt, s_user_data);
            }
            break;

        case ESP_WEBRTC_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "WebRTC disconnected");
            s_connected = false;
            s_data_channel_open = false;
            if (s_event_cb) {
                webrtc_azure_event_t evt = {.type = WEBRTC_AZURE_EVENT_DISCONNECTED};
                s_event_cb(&evt, s_user_data);
            }
            break;

        case ESP_WEBRTC_EVENT_DATA_CHANNEL_CONNECTED:
            ESP_LOGI(TAG, "Data channel connected - sending initial message");
            s_data_channel_open = true;

            // Send initial instructions
            send_response(AZURE_OPENAI_INSTRUCTIONS);

            // Send function descriptions
            send_function_desc();

            if (s_event_cb) {
                webrtc_azure_event_t evt = {.type = WEBRTC_AZURE_EVENT_DATA_CHANNEL_OPEN};
                s_event_cb(&evt, s_user_data);
            }
            break;

        default:
            break;
    }
    return 0;
}

// ============================================
// Public API
// ============================================

esp_err_t webrtc_azure_init(webrtc_azure_config_t *config)
{
    ESP_LOGI(TAG, "============================================================");
    ESP_LOGI(TAG, "         WEBRTC AZURE INIT                                  ");
    ESP_LOGI(TAG, "============================================================");

    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized, skipping");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Free heap at init start: %lu bytes", esp_get_free_heap_size());

    // Register thread scheduler callback (only affects pc_task)
    ESP_LOGI(TAG, "Registering thread scheduler callback...");
    media_lib_thread_set_schedule_cb(webrtc_thread_scheduler);

    if (config) {
        ESP_LOGI(TAG, "Event callback registered: %p", config->event_cb);
        s_event_cb = config->event_cb;
        s_user_data = config->user_data;
    } else {
        ESP_LOGW(TAG, "No config provided, event callback not set");
    }

    // Build function calling classes
    ESP_LOGI(TAG, "Building function calling classes...");
    build_classes();

    // Build media system
    ESP_LOGI(TAG, "Building media system...");
    int media_ret = media_sys_buildup();
    ESP_LOGI(TAG, "media_sys_buildup returned: %d", media_ret);

    s_initialized = true;
    ESP_LOGI(TAG, "Free heap after init: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "WebRTC Azure module initialized successfully");
    return ESP_OK;
}

esp_err_t webrtc_azure_start(void)
{
    ESP_LOGI(TAG, "============================================================");
    ESP_LOGI(TAG, "         WEBRTC AZURE START                                 ");
    ESP_LOGI(TAG, "============================================================");

    if (!s_initialized) {
        ESP_LOGE(TAG, "Module not initialized! Call webrtc_azure_init() first.");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_webrtc) {
        ESP_LOGW(TAG, "WebRTC already running (s_webrtc=%p), stopping first...", s_webrtc);
        webrtc_azure_stop();
        ESP_LOGI(TAG, "Previous WebRTC instance stopped");
    }

    ESP_LOGI(TAG, "Starting WebRTC connection to Azure OpenAI...");
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());

    // Configure peer
    esp_peer_default_cfg_t peer_cfg = {
        .agent_recv_timeout = 500,
    };

    // Configure signaling (for OpenAI direct connection if not using Azure)
    typedef struct {
        char *token;
        char *voice;
    } openai_signaling_cfg_t;

    openai_signaling_cfg_t openai_cfg = {
        .token = OPENAI_API_KEY,
        .voice = "alloy",
    };

    // Configure WebRTC
    ESP_LOGI(TAG, "Configuring WebRTC...");
#ifdef WEBRTC_SUPPORT_OPUS
    ESP_LOGI(TAG, "  Audio codec: OPUS (16kHz, mono)");
#else
    ESP_LOGI(TAG, "  Audio codec: G.711A");
#endif
    ESP_LOGI(TAG, "  Data channel: %s", DATA_CHANNEL_ENABLED ? "enabled" : "disabled");

    esp_webrtc_cfg_t cfg = {
        .peer_cfg = {
            .audio_info = {
#ifdef WEBRTC_SUPPORT_OPUS
                .codec = ESP_PEER_AUDIO_CODEC_OPUS,
                .sample_rate = 16000,
                .channel = 1,
#else
                .codec = ESP_PEER_AUDIO_CODEC_G711A,
#endif
            },
            .audio_dir = ESP_PEER_MEDIA_DIR_SEND_RECV,
            .enable_data_channel = DATA_CHANNEL_ENABLED,
            .on_custom_data = webrtc_data_handler,
            .extra_cfg = &peer_cfg,
            .extra_size = sizeof(peer_cfg),
        },
        .signaling_cfg.extra_cfg = &openai_cfg,
        .signaling_cfg.extra_size = sizeof(openai_cfg),
        .peer_impl = esp_peer_get_default_impl(),
        .signaling_impl = esp_signaling_get_openai_signaling(),
    };

    ESP_LOGI(TAG, "Calling esp_webrtc_open()...");
    int ret = esp_webrtc_open(&cfg, &s_webrtc);
    if (ret != 0) {
        ESP_LOGE(TAG, "esp_webrtc_open() FAILED: ret=%d", ret);
        ESP_LOGE(TAG, "Possible causes:");
        ESP_LOGE(TAG, "  1. Memory allocation failure");
        ESP_LOGE(TAG, "  2. Signaling initialization failed (check OPENAI_SIGNALING logs above)");
        ESP_LOGE(TAG, "  3. Peer implementation initialization failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "esp_webrtc_open() SUCCESS: s_webrtc=%p", s_webrtc);

    // Set media provider
    ESP_LOGI(TAG, "Setting media provider...");
    esp_webrtc_media_provider_t media_provider = {};
    int media_ret = media_sys_get_provider(&media_provider);
    ESP_LOGI(TAG, "media_sys_get_provider returned: %d", media_ret);
    ESP_LOGI(TAG, "  capture: %p", media_provider.capture);
    ESP_LOGI(TAG, "  player: %p", media_provider.player);
    esp_webrtc_set_media_provider(s_webrtc, &media_provider);
    ESP_LOGI(TAG, "Media provider set");

    // Set event handler
    ESP_LOGI(TAG, "Setting event handler...");
    esp_webrtc_set_event_handler(s_webrtc, webrtc_event_handler, NULL);

    // Start WebRTC
    ESP_LOGI(TAG, "Calling esp_webrtc_start()...");
    ESP_LOGI(TAG, "Free heap before start: %lu bytes", esp_get_free_heap_size());
    ret = esp_webrtc_start(s_webrtc);
    if (ret != 0) {
        ESP_LOGE(TAG, "esp_webrtc_start() FAILED: ret=%d", ret);
        ESP_LOGE(TAG, "Possible causes:");
        ESP_LOGE(TAG, "  1. DTLS/SRTP initialization failed");
        ESP_LOGE(TAG, "  2. ICE agent failed to start");
        ESP_LOGE(TAG, "  3. Media system initialization failed");
        esp_webrtc_close(s_webrtc);
        s_webrtc = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "============================================================");
    ESP_LOGI(TAG, "esp_webrtc_start() SUCCESS");
    ESP_LOGI(TAG, "WebRTC is now running - waiting for connection events...");
    ESP_LOGI(TAG, "Free heap after start: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "============================================================");
    return ESP_OK;
}

esp_err_t webrtc_azure_stop(void)
{
    if (s_webrtc) {
        ESP_LOGI(TAG, "Stopping WebRTC...");
        esp_webrtc_handle_t handle = s_webrtc;
        s_webrtc = NULL;
        s_connected = false;
        s_data_channel_open = false;
        esp_webrtc_close(handle);
        ESP_LOGI(TAG, "WebRTC stopped");
    }
    return ESP_OK;
}

esp_err_t webrtc_azure_send_text(const char *text)
{
    if (s_webrtc == NULL) {
        ESP_LOGE(TAG, "WebRTC not started");
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_data_channel_open) {
        ESP_LOGE(TAG, "Data channel not open");
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *root = cJSON_CreateObject();
    if (root) {
        cJSON_AddStringToObject(root, "type", "conversation.item.create");
        cJSON_AddNullToObject(root, "previous_item_id");
        cJSON *item = cJSON_CreateObject();
        if (item) {
            cJSON_AddStringToObject(item, "type", "message");
            cJSON_AddStringToObject(item, "role", "user");
        }
        cJSON *contentArray = cJSON_CreateArray();
        cJSON *contentItem = cJSON_CreateObject();
        cJSON_AddStringToObject(contentItem, "type", "input_text");
        cJSON_AddStringToObject(contentItem, "text", text);
        cJSON_AddItemToArray(contentArray, contentItem);
        cJSON_AddItemToObject(item, "content", contentArray);
        cJSON_AddItemToObject(root, "item", item);
    }

    char *send_text = cJSON_Print(root);
    if (send_text) {
        ESP_LOGI(TAG, "Sending text: %s", send_text);
        esp_webrtc_send_custom_data(s_webrtc, ESP_WEBRTC_CUSTOM_DATA_VIA_DATA_CHANNEL,
                                    (uint8_t *)send_text, strlen(send_text));
        free(send_text);
    }
    cJSON_Delete(root);
    return ESP_OK;
}

bool webrtc_azure_is_connected(void)
{
    return s_connected && s_data_channel_open;
}

bool webrtc_azure_is_running(void)
{
    return s_webrtc != NULL;
}

void webrtc_azure_query(void)
{
    if (s_webrtc) {
        esp_webrtc_query(s_webrtc);
    }
}

void webrtc_azure_deinit(void)
{
    webrtc_azure_stop();
    s_initialized = false;
    s_event_cb = NULL;
    s_user_data = NULL;
    ESP_LOGI(TAG, "WebRTC Azure module deinitialized");
}
