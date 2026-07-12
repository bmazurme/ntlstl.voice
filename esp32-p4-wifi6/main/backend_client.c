#include "backend_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "wav.h"

static const char *TAG = "backend_client";

#define BOUNDARY "----esp32p4boundary"
#define MAX_RESP_LEN (16 * 1024)

// CONFIG_P4_BACKEND_URL points at .../api/transcribe; derive the shared
// base URL for the other two endpoints by stripping that suffix.
static void get_base_url(char *out, size_t out_len)
{
    static const char *suffix = "/api/transcribe";

    strncpy(out, CONFIG_P4_BACKEND_URL, out_len - 1);
    out[out_len - 1] = '\0';

    size_t url_len = strlen(out);
    size_t suffix_len = strlen(suffix);
    if (url_len >= suffix_len && strcmp(out + url_len - suffix_len, suffix) == 0) {
        out[url_len - suffix_len] = '\0';
    }
}

// POSTs `body` and captures the response into a NUL-terminated buffer the
// caller owns and must free(). Returns ESP_OK only on a 2xx status.
static esp_err_t http_post(const char *url, const char *content_type,
                            const void *body, size_t body_len, char **out_resp)
{
    *out_resp = NULL;

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 60000, // whisper/Ollama on modest hardware can be slow
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client for %s", url);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", content_type);

    esp_err_t err = esp_http_client_open(client, (int)body_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open connection to %s: %s", url, esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    if (body_len > 0) {
        esp_http_client_write(client, (const char *)body, (int)body_len);
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);

    char *resp = NULL;
    if (content_length > 0) {
        size_t to_read = content_length < MAX_RESP_LEN ? (size_t)content_length : MAX_RESP_LEN;
        resp = malloc(to_read + 1);
        if (resp) {
            int read_len = esp_http_client_read_response(client, resp, (int)to_read);
            resp[read_len > 0 ? read_len : 0] = '\0';
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "POST %s -> HTTP %d", url, status);
    *out_resp = resp;
    return (status >= 200 && status < 300) ? ESP_OK : ESP_FAIL;
}

// Step 1: upload the recorded clip as a WAV file, return the transcript
// (heap-allocated, caller frees) or NULL on failure/empty transcript.
static char *transcribe(const char *base_url, const int16_t *samples, size_t num_samples, uint32_t sample_rate)
{
    uint32_t pcm_bytes = (uint32_t)(num_samples * sizeof(int16_t));
    uint8_t wav_header[WAV_HEADER_SIZE];
    wav_write_header(wav_header, pcm_bytes, sample_rate, 1, 16);

    char part_head[160];
    int part_head_len = snprintf(part_head, sizeof(part_head),
        "--" BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"audio\"; filename=\"clip.wav\"\r\n"
        "Content-Type: audio/wav\r\n\r\n");
    char part_tail[64];
    int part_tail_len = snprintf(part_tail, sizeof(part_tail), "\r\n--" BOUNDARY "--\r\n");
    size_t total_len = part_head_len + WAV_HEADER_SIZE + pcm_bytes + part_tail_len;

    char url[224];
    snprintf(url, sizeof(url), "%s/api/transcribe", base_url);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 60000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return NULL;
    }
    esp_http_client_set_header(client, "Content-Type", "multipart/form-data; boundary=" BOUNDARY);

    esp_err_t err = esp_http_client_open(client, (int)total_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open /api/transcribe: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return NULL;
    }

    esp_http_client_write(client, part_head, part_head_len);
    esp_http_client_write(client, (const char *)wav_header, WAV_HEADER_SIZE);
    esp_http_client_write(client, (const char *)samples, (int)pcm_bytes);
    esp_http_client_write(client, part_tail, part_tail_len);

    int content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);

    char *resp = NULL;
    if (content_length > 0) {
        size_t to_read = content_length < MAX_RESP_LEN ? (size_t)content_length : MAX_RESP_LEN;
        resp = malloc(to_read + 1);
        if (resp) {
            int read_len = esp_http_client_read_response(client, resp, (int)to_read);
            resp[read_len > 0 ? read_len : 0] = '\0';
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "POST %s -> HTTP %d", url, status);

    if (status < 200 || status >= 300 || !resp) {
        ESP_LOGW(TAG, "Transcribe failed");
        free(resp);
        return NULL;
    }

    cJSON *root = cJSON_Parse(resp);
    free(resp);
    if (!root) {
        ESP_LOGW(TAG, "Transcribe response was not valid JSON");
        return NULL;
    }

    char *text = NULL;
    cJSON *text_item = cJSON_GetObjectItemCaseSensitive(root, "text");
    if (cJSON_IsString(text_item) && text_item->valuestring[0] != '\0') {
        text = strdup(text_item->valuestring);
        ESP_LOGI(TAG, "Transcript: %s", text);
    } else {
        ESP_LOGW(TAG, "Empty/missing transcript in response");
    }
    cJSON_Delete(root);
    return text;
}

// Step 2: turn the transcript into a structured HA command. Returns a
// heap-allocated compact JSON string of the "command" object (caller
// frees with cJSON_free), or NULL on failure.
static char *llm_command(const char *base_url, const char *text)
{
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "text", text);
    char *req_body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!req_body) {
        return NULL;
    }

    char url[224];
    snprintf(url, sizeof(url), "%s/api/llm-command", base_url);

    char *resp = NULL;
    esp_err_t err = http_post(url, "application/json", req_body, strlen(req_body), &resp);
    cJSON_free(req_body);

    if (err != ESP_OK || !resp) {
        ESP_LOGW(TAG, "llm-command failed");
        free(resp);
        return NULL;
    }

    cJSON *root = cJSON_Parse(resp);
    free(resp);
    if (!root) {
        ESP_LOGW(TAG, "llm-command response was not valid JSON");
        return NULL;
    }

    cJSON *command = cJSON_GetObjectItemCaseSensitive(root, "command");
    char *command_json = command ? cJSON_PrintUnformatted(command) : NULL;
    if (command_json) {
        ESP_LOGI(TAG, "Command: %s", command_json);
    } else {
        ESP_LOGW(TAG, "No command in llm-command response");
    }
    cJSON_Delete(root);
    return command_json;
}

// Step 3: execute the command in Home Assistant. Not every command results
// in a device action (e.g. action "query"/"unknown") - the backend reports
// that as a normal 200 with executed:false, which is not an error here.
static void ha_execute(const char *base_url, const char *command_json)
{
    char url[224];
    snprintf(url, sizeof(url), "%s/api/ha-execute", base_url);

    char *resp = NULL;
    esp_err_t err = http_post(url, "application/json", command_json, strlen(command_json), &resp);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ha-execute failed");
    } else if (resp) {
        ESP_LOGI(TAG, "ha-execute response: %s", resp);
    }
    free(resp);
}

esp_err_t backend_client_run_pipeline(const int16_t *samples, size_t num_samples, uint32_t sample_rate)
{
    char base_url[192];
    get_base_url(base_url, sizeof(base_url));

    char *text = transcribe(base_url, samples, num_samples, sample_rate);
    if (!text) {
        return ESP_FAIL;
    }

    char *command_json = llm_command(base_url, text);
    free(text);
    if (!command_json) {
        return ESP_FAIL;
    }

    ha_execute(base_url, command_json);
    cJSON_free(command_json);
    return ESP_OK;
}
