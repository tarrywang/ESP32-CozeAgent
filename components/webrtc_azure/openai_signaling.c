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
#include "esp_webrtc.h"
#include "esp_log.h"
#include <cJSON.h>
#include "webrtc_azure_settings.h"

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
    ESP_LOGI(TAG, "========== Azure Sessions Response ==========");
    ESP_LOGI(TAG, "HTTP Response size=%d", resp->size);

    if (resp->size == 0 || resp->data == NULL) {
        ESP_LOGE(TAG, "Empty response from Azure!");
        return;
    }

    // Log full response for debugging (up to 1000 chars)
    ESP_LOGI(TAG, "Response: %.*s", resp->size > 1000 ? 1000 : resp->size, (char *)resp->data);

    // Check for error in response (Azure returns {"error": {...}} on failure)
    if (strstr((char *)resp->data, "\"error\"") != NULL) {
        ESP_LOGE(TAG, "Azure API returned error! Full response above.");
        return;
    }

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
    ESP_LOGI(TAG, "========== Step 1: Getting Azure Ephemeral Token ==========");
    ESP_LOGI(TAG, "URL: %s", AZURE_SESSIONS_URL);
    ESP_LOGI(TAG, "Endpoint: %s", AZURE_OPENAI_ENDPOINT);
    ESP_LOGI(TAG, "Deployment: %s", AZURE_OPENAI_DEPLOYMENT);
    ESP_LOGI(TAG, "API Version: %s", AZURE_OPENAI_API_VERSION);

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

    ESP_LOGI(TAG, "Request body length: %d bytes", json_string ? (int)strlen(json_string) : 0);
    // Don't log full instructions to avoid flooding
    ESP_LOGI(TAG, "Voice: %s, Model: %s", voice, AZURE_OPENAI_DEPLOYMENT);

    if (json_string) {
        ESP_LOGI(TAG, "Sending HTTPS POST request...");
        int ret = https_post(AZURE_SESSIONS_URL, header, json_string, NULL, azure_client_secret_callback, sig);
        if (ret != 0) {
            ESP_LOGE(TAG, "HTTPS POST failed! ret=%d", ret);
            ESP_LOGE(TAG, "Check network connection and Azure endpoint configuration");
        } else {
            ESP_LOGI(TAG, "HTTPS POST returned successfully (callback should have been called)");
        }
        free(json_string);
    } else {
        ESP_LOGE(TAG, "Failed to create JSON request body!");
    }
    cJSON_Delete(root);

    // Log result
    if (sig->client_secret) {
        ESP_LOGI(TAG, "Step 1 SUCCESS: Got ephemeral token (length=%d)", (int)strlen(sig->client_secret));
    } else {
        ESP_LOGE(TAG, "Step 1 FAILED: No ephemeral token obtained!");
    }
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
    ESP_LOGI(TAG, "============================================================");
    ESP_LOGI(TAG, "         OPENAI SIGNALING START                             ");
    ESP_LOGI(TAG, "============================================================");

    openai_signaling_t *sig = (openai_signaling_t *)calloc(1, sizeof(openai_signaling_t));
    if (sig == NULL) {
        ESP_LOGE(TAG, "Failed to allocate signaling structure!");
        return ESP_PEER_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Signaling structure allocated at %p", sig);
    sig->cfg = *cfg;

#ifdef USE_AZURE_OPENAI
    // Azure OpenAI: Get client_secret using api-key
    ESP_LOGI(TAG, "Mode: Azure OpenAI Realtime API (WebRTC)");
    ESP_LOGI(TAG, "Region: %s", AZURE_OPENAI_REGION);
    get_azure_client_secret(sig, "alloy");
    if (sig->client_secret == NULL) {
        ESP_LOGE(TAG, "============================================================");
        ESP_LOGE(TAG, "FATAL: Failed to get Azure client_secret!");
        ESP_LOGE(TAG, "Possible causes:");
        ESP_LOGE(TAG, "  1. Invalid API Key");
        ESP_LOGE(TAG, "  2. Network connectivity issue");
        ESP_LOGE(TAG, "  3. Azure endpoint not reachable");
        ESP_LOGE(TAG, "  4. Deployment not configured for Realtime API");
        ESP_LOGE(TAG, "============================================================");
        free(sig);
        return ESP_PEER_ERR_NOT_SUPPORT;
    }
    ESP_LOGI(TAG, "Azure authentication successful!");
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
    ESP_LOGI(TAG, "========== SDP Answer Callback ==========");
    ESP_LOGI(TAG, "Response size=%d bytes", resp->size);

    if (resp->size == 0 || resp->data == NULL) {
        ESP_LOGE(TAG, "Empty SDP answer response!");
        return;
    }

    // Log first part of SDP answer
    ESP_LOGI(TAG, "SDP Answer (first 500 chars): %.*s", resp->size > 500 ? 500 : resp->size, (char *)resp->data);

    // Check if it's an error response (JSON with error)
    if (resp->data[0] == '{') {
        ESP_LOGE(TAG, "Received JSON instead of SDP! Likely an error response.");
        ESP_LOGE(TAG, "Full response: %.*s", resp->size > 1000 ? 1000 : resp->size, (char *)resp->data);
        return;
    }

    SAFE_FREE(sig->remote_sdp);
    sig->remote_sdp = (uint8_t *)malloc(resp->size);
    if (sig->remote_sdp == NULL) {
        ESP_LOGE(TAG, "No enough memory for remote sdp (need %d bytes)", resp->size);
        return;
    }
    memcpy(sig->remote_sdp, resp->data, resp->size);
    sig->remote_sdp_size = resp->size;
    ESP_LOGI(TAG, "Step 2 SUCCESS: SDP answer stored (%d bytes)", resp->size);
}

static int openai_signaling_send_msg(esp_peer_signaling_handle_t h, esp_peer_signaling_msg_t *msg)
{
    openai_signaling_t *sig = (openai_signaling_t *)h;
    ESP_LOGI(TAG, "openai_signaling_send_msg called, msg->type=%d", msg->type);

    if (msg->type == ESP_PEER_SIGNALING_MSG_BYE) {
        ESP_LOGI(TAG, "Received BYE message");
        // Handle bye message if needed
    } else if (msg->type == ESP_PEER_SIGNALING_MSG_SDP) {
        ESP_LOGI(TAG, "========== Step 2: SDP Exchange ==========");
        ESP_LOGI(TAG, "Local SDP size: %d bytes", msg->size);
        ESP_LOGI(TAG, "Local SDP (first 300 chars): %.*s", msg->size > 300 ? 300 : msg->size, (char *)msg->data);

        char content_type[] = "Content-Type: application/sdp";

#ifdef USE_AZURE_OPENAI
        // Azure: Use Bearer token with client_secret, POST to /realtimeapi/webrtc
        ESP_LOGI(TAG, "WebRTC endpoint URL: %s", AZURE_WEBRTC_URL);
        ESP_LOGI(TAG, "Using ephemeral token for authorization");

        char auth_header[512];
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", sig->client_secret);
        char *header[] = {
            content_type,
            auth_header,
            NULL,
        };

        ESP_LOGI(TAG, "Sending SDP offer to Azure WebRTC endpoint...");
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

        ESP_LOGI(TAG, "HTTPS POST returned: %d", ret);

        if (ret != 0) {
            ESP_LOGE(TAG, "HTTPS POST failed with error code: %d", ret);
            ESP_LOGE(TAG, "SDP exchange failed - check network and WebRTC endpoint");
            return -1;
        }

        if (sig->remote_sdp == NULL) {
            ESP_LOGE(TAG, "No SDP answer received (callback may have failed)");
            ESP_LOGE(TAG, "Possible causes:");
            ESP_LOGE(TAG, "  1. Invalid ephemeral token");
            ESP_LOGE(TAG, "  2. WebRTC endpoint error");
            ESP_LOGE(TAG, "  3. Invalid local SDP format");
            return -1;
        }

        ESP_LOGI(TAG, "SDP exchange successful! Delivering remote SDP to peer...");
        esp_peer_signaling_msg_t sdp_msg = {
            .type = ESP_PEER_SIGNALING_MSG_SDP,
            .data = sig->remote_sdp,
            .size = sig->remote_sdp_size,
        };
        sig->cfg.on_msg(&sdp_msg, sig->cfg.ctx);
        ESP_LOGI(TAG, "Remote SDP delivered to peer connection");
    } else {
        ESP_LOGW(TAG, "Unknown message type: %d", msg->type);
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
