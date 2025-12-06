/* OpenAI signaling

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "https_client.h"
#include "common.h"
#include "esp_log.h"
#include <cJSON.h>
#include "settings.h"

#define TAG                   "OPENAI_SIGNALING"

#ifdef USE_AZURE_OPENAI
// Azure OpenAI Realtime API
// Step 1: Get ephemeral token from resource endpoint /openai/realtimeapi/sessions
// Step 2: POST SDP to regional WebRTC endpoint /v1/realtimertc
#define AZURE_SESSIONS_URL "https://" AZURE_OPENAI_ENDPOINT "/openai/realtimeapi/sessions?api-version=" AZURE_OPENAI_API_VERSION "&deployment=" AZURE_OPENAI_DEPLOYMENT
#define AZURE_WEBRTC_URL   "https://" AZURE_OPENAI_REGION ".realtimeapi-preview.ai.azure.com/v1/realtimertc"
#else
// OpenAI Realtime API
#define OPENAI_REALTIME_MODEL "gpt-4o-mini-realtime-preview-2024-12-17"
#define OPENAI_SESSIONS_URL   "https://api.openai.com/v1/realtime/sessions"
#define OPENAI_REALTIME_URL   "https://api.openai.com/v1/realtime?model=" OPENAI_REALTIME_MODEL
#endif

#define SAFE_FREE(p) if (p) {   \
    free(p);                    \
    p = NULL;                   \
}

typedef struct {
    esp_peer_signaling_cfg_t cfg;
    uint8_t                 *remote_sdp;
    int                      remote_sdp_size;
    char                    *client_secret;  // ephemeral token from client_secrets API
} openai_signaling_t;

#ifdef USE_AZURE_OPENAI
// Azure: Parse JSON response to get nested "client_secret.value" field
static void azure_client_secret_callback(http_resp_t *resp, void *ctx)
{
    openai_signaling_t *sig = (openai_signaling_t *)ctx;
    ESP_LOGI(TAG, "Azure sessions response, size=%d", resp->size);
    ESP_LOGI(TAG, "Response: %.*s", resp->size > 500 ? 500 : resp->size, (char *)resp->data);

    cJSON *root = cJSON_Parse((char *)resp->data);
    if (root) {
        // Response format: {"client_secret": {"value": "ek_xxx", "expires_at": 123}}
        cJSON *client_secret = cJSON_GetObjectItem(root, "client_secret");
        if (client_secret) {
            cJSON *value = cJSON_GetObjectItem(client_secret, "value");
            if (value && cJSON_IsString(value)) {
                sig->client_secret = strdup(value->valuestring);
                ESP_LOGI(TAG, "Got Azure client_secret successfully");
            } else {
                ESP_LOGE(TAG, "No 'value' field in client_secret");
            }
        } else {
            ESP_LOGE(TAG, "No 'client_secret' field in response");
        }
        cJSON_Delete(root);
    } else {
        ESP_LOGE(TAG, "Failed to parse JSON response");
    }
}

static void get_azure_client_secret(openai_signaling_t *sig, const char *voice)
{
    ESP_LOGI(TAG, "Getting Azure client_secret from: %s", AZURE_SESSIONS_URL);

    char api_key_header[128];
    snprintf(api_key_header, sizeof(api_key_header), "api-key: %s", AZURE_OPENAI_API_KEY);
    char content_type[] = "Content-Type: application/json";
    char *header[] = {
        api_key_header,
        content_type,
        NULL,
    };

    // Request body with instructions: {"model":"gpt-realtime","voice":"alloy","instructions":"..."}
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", AZURE_OPENAI_DEPLOYMENT);
    cJSON_AddStringToObject(root, "voice", voice);
    cJSON_AddStringToObject(root, "instructions", AZURE_OPENAI_INSTRUCTIONS);

    char *json_string = cJSON_Print(root);

    ESP_LOGI(TAG, "Request body: %s", json_string);

    if (json_string) {
        int ret = https_post(AZURE_SESSIONS_URL, header, json_string, NULL, azure_client_secret_callback, sig);
        if (ret != 0) {
            ESP_LOGE(TAG, "Failed to get client_secret, ret=%d", ret);
        }
        free(json_string);
    }
    cJSON_Delete(root);
}

#else
// OpenAI: Parse nested JSON response to get client_secret.value
#define GET_KEY_END(str, key) get_key_end(str, key, sizeof(key) - 1)

static char *get_key_end(char *str, char *key, int len)
{
    char *p = strstr(str, key);
    if (p == NULL) {
        return NULL;
    }
    return p + len;
}

static void openai_session_callback(http_resp_t *resp, void *ctx)
{
    openai_signaling_t *sig = (openai_signaling_t *)ctx;
    char *token = GET_KEY_END((char *)resp->data, "\"client_secret\"");
    if (token == NULL) {
        return;
    }
    char *secret = GET_KEY_END(token, "\"value\"");
    if (secret == NULL) {
        return;
    }
    char *s = strchr(secret, '"');
    if (s == NULL) {
        return;
    }
    s++;
    char *e = strchr(s, '"');
    *e = 0;
    sig->client_secret = strdup(s);
    *e = '"';
}

static void get_openai_ephemeral_token(openai_signaling_t *sig, char *token, char *voice)
{
    char content_type[32] = "Content-Type: application/json";
    int len = strlen("Authorization: Bearer ") + strlen(token) + 1;
    char auth[len];
    snprintf(auth, len, "Authorization: Bearer %s", token);
    char *header[] = {
        content_type,
        auth,
        NULL,
    };
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", OPENAI_REALTIME_MODEL);
    cJSON *modalities = cJSON_CreateArray();
    cJSON_AddItemToArray(modalities, cJSON_CreateString("text"));
    cJSON_AddItemToArray(modalities, cJSON_CreateString("audio"));
    cJSON_AddItemToObject(root, "modalities", modalities);
    cJSON_AddStringToObject(root, "voice", voice);
    char *json_string = cJSON_Print(root);
    if (json_string) {
        https_post(OPENAI_SESSIONS_URL, header, json_string, NULL, openai_session_callback, sig);
        free(json_string);
    }
    cJSON_Delete(root);
}
#endif

static int openai_signaling_start(esp_peer_signaling_cfg_t *cfg, esp_peer_signaling_handle_t *h)
{
    openai_signaling_t *sig = (openai_signaling_t *)calloc(1, sizeof(openai_signaling_t));
    if (sig == NULL) {
        return ESP_PEER_ERR_NO_MEM;
    }
    sig->cfg = *cfg;

#ifdef USE_AZURE_OPENAI
    // Azure OpenAI: Get client_secret using api-key
    ESP_LOGI(TAG, "Using Azure OpenAI Realtime API");
    get_azure_client_secret(sig, "alloy");
    if (sig->client_secret == NULL) {
        ESP_LOGE(TAG, "Failed to get Azure client_secret");
        free(sig);
        return ESP_PEER_ERR_NOT_SUPPORT;
    }
#else
    // OpenAI: Get ephemeral token using API key
    ESP_LOGI(TAG, "Using OpenAI Realtime API");
    openai_signaling_cfg_t *openai_cfg = (openai_signaling_cfg_t *)cfg->extra_cfg;
    // alloy, ash, ballad, coral, echo, sage, shimmer and verse
    get_openai_ephemeral_token(sig, openai_cfg->token, openai_cfg->voice ? openai_cfg->voice : "alloy");
    if (sig->client_secret == NULL) {
        free(sig);
        return ESP_PEER_ERR_NOT_SUPPORT;
    }
#endif

    *h = sig;
    esp_peer_signaling_ice_info_t ice_info = {
        .is_initiator = true,
    };
    sig->cfg.on_ice_info(&ice_info, sig->cfg.ctx);
    sig->cfg.on_connected(sig->cfg.ctx);
    return ESP_PEER_ERR_NONE;
}

static void openai_sdp_answer(http_resp_t *resp, void *ctx)
{
    openai_signaling_t *sig = (openai_signaling_t *)ctx;
    ESP_LOGI(TAG, "Got SDP answer, size=%d", resp->size);
    ESP_LOGI(TAG, "Response: %.*s", resp->size > 500 ? 500 : resp->size, (char *)resp->data);

    SAFE_FREE(sig->remote_sdp);
    sig->remote_sdp = (uint8_t *)malloc(resp->size);
    if (sig->remote_sdp == NULL) {
        ESP_LOGE(TAG, "No enough memory for remote sdp");
        return;
    }
    memcpy(sig->remote_sdp, resp->data, resp->size);
    sig->remote_sdp_size = resp->size;
}

static int openai_signaling_send_msg(esp_peer_signaling_handle_t h, esp_peer_signaling_msg_t *msg)
{
    openai_signaling_t *sig = (openai_signaling_t *)h;
    if (msg->type == ESP_PEER_SIGNALING_MSG_BYE) {
        // Handle bye message if needed
    } else if (msg->type == ESP_PEER_SIGNALING_MSG_SDP) {
        ESP_LOGI(TAG, "Sending local SDP to server");
        char content_type[] = "Content-Type: application/sdp";

#ifdef USE_AZURE_OPENAI
        // Azure: Use Bearer token with client_secret, POST to /realtimeapi/webrtc
        ESP_LOGI(TAG, "POST to: %s", AZURE_WEBRTC_URL);
        char auth_header[512];
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", sig->client_secret);
        char *header[] = {
            content_type,
            auth_header,
            NULL,
        };
        int ret = https_post(AZURE_WEBRTC_URL, header, (char *)msg->data, NULL, openai_sdp_answer, h);
#else
        // OpenAI: Use Bearer token with ephemeral token
        ESP_LOGI(TAG, "POST to: %s", OPENAI_REALTIME_URL);
        char *token = sig->client_secret;
        int len = strlen("Authorization: Bearer ") + strlen(token) + 1;
        char auth[len];
        snprintf(auth, len, "Authorization: Bearer %s", token);
        char *header[] = {
            content_type,
            auth,
            NULL,
        };
        int ret = https_post(OPENAI_REALTIME_URL, header, (char *)msg->data, NULL, openai_sdp_answer, h);
#endif

        if (ret != 0 || sig->remote_sdp == NULL) {
            ESP_LOGE(TAG, "Failed to post SDP, ret=%d", ret);
            return -1;
        }
        esp_peer_signaling_msg_t sdp_msg = {
            .type = ESP_PEER_SIGNALING_MSG_SDP,
            .data = sig->remote_sdp,
            .size = sig->remote_sdp_size,
        };
        sig->cfg.on_msg(&sdp_msg, sig->cfg.ctx);
    }
    return 0;
}

static int openai_signaling_stop(esp_peer_signaling_handle_t h)
{
    openai_signaling_t *sig = (openai_signaling_t *)h;
    sig->cfg.on_close(sig->cfg.ctx);
    SAFE_FREE(sig->remote_sdp);
    SAFE_FREE(sig->client_secret);
    SAFE_FREE(sig);
    return 0;
}

const esp_peer_signaling_impl_t *esp_signaling_get_openai_signaling(void)
{
    static const esp_peer_signaling_impl_t impl = {
        .start = openai_signaling_start,
        .send_msg = openai_signaling_send_msg,
        .stop = openai_signaling_stop,
    };
    return &impl;
}
