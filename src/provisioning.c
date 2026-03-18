//
//  provisioning.c
//  Zero-touch provisioning через libcurl
//
//  Зависимости: libcurl, cJSON (уже используется в проекте)
//  CMakeLists.txt: target_link_libraries(smarthome-hub ... curl)
//

#include "provisioning.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "cJSON.h"

// ── HTTP response buffer ──────────────────────────────────────────────────────

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} response_buf_t;

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    response_buf_t *buf = (response_buf_t *)userdata;
    size_t incoming = size * nmemb;

    // Расширяем буфер если нужно
    if (buf->len + incoming + 1 > buf->cap) {
        size_t new_cap = buf->cap + incoming + 4096;
        char *tmp = realloc(buf->data, new_cap);
        if (!tmp) return 0;
        buf->data = tmp;
        buf->cap  = new_cap;
    }

    memcpy(buf->data + buf->len, ptr, incoming);
    buf->len += incoming;
    buf->data[buf->len] = '\0';
    return incoming;
}

// ── Основная функция провижинга ───────────────────────────────────────────────

int hub_provision(hub_config_t *cfg, const char *config_path)
{
    LOG_INFO("provision", "Starting zero-touch provisioning...");
    LOG_INFO("provision", "Serial: %s", cfg->hub_id);
    LOG_INFO("provision", "Cloud:  %s", cfg->cloud_host);

    // Формируем URL для регистрации
    // Используем cloud_api_url если задан, иначе строим из cloud_host
    char url[512];
    if (cfg->cloud_api_url[0]) {
        snprintf(url, sizeof(url), "%s/api/hub/register", cfg->cloud_api_url);
    } else {
        snprintf(url, sizeof(url), "%s://%s:%d/api/hub/register",
                 cfg->cloud_use_tls ? "https" : "http",
                 cfg->cloud_host,
                 cfg->cloud_port);
    }
    LOG_INFO("provision", "POST %s", url);

    // Формируем JSON тело запроса
    // serial = текущий hub_id (заводской номер вида HUB-000000000000)
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "serial",   cfg->hub_id);
    cJSON_AddStringToObject(body, "name",     "SmartHome Hub");
    cJSON_AddStringToObject(body, "firmware", "1.0.0");
    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);

    if (!body_str) {
        LOG_ERROR("provision", "Failed to build JSON body");
        return -1;
    }

    LOG_DEBUG("provision", "Request body: %s", body_str);

    // Инициализируем curl
    CURL *curl = curl_easy_init();
    if (!curl) {
        LOG_ERROR("provision", "curl_easy_init failed");
        free(body_str);
        return -1;
    }

    // Буфер для ответа
    response_buf_t resp = {0};
    resp.data = malloc(4096);
    if (!resp.data) {
        curl_easy_cleanup(curl);
        free(body_str);
        return -1;
    }
    resp.data[0] = '\0';
    resp.cap = 4096;

    // HTTP заголовки
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");

    curl_easy_setopt(curl, CURLOPT_URL,            url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,     body_str);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,     headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    // TLS настройки
    if (cfg->cloud_use_tls) {
        // В продакшене: CURLOPT_SSL_VERIFYPEER = 1
        // На Raspberry Pi с самоподписанным сертификатом — отключаем
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    // Выполняем запрос
    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(body_str);

    // Проверяем результат
    if (res != CURLE_OK) {
        LOG_ERROR("provision", "curl error: %s", curl_easy_strerror(res));
        free(resp.data);
        return -1;
    }

    LOG_DEBUG("provision", "HTTP %ld, response: %s", http_code, resp.data);

    if (http_code != 200 && http_code != 201) {
        LOG_ERROR("provision", "Server returned HTTP %ld", http_code);
        free(resp.data);
        return -1;
    }

    // Парсим ответ
    cJSON *root = cJSON_Parse(resp.data);
    free(resp.data);

    if (!root) {
        LOG_ERROR("provision", "Failed to parse server response");
        return -1;
    }

    // Извлекаем hub_id (UUID) и token (JWT)
    cJSON *hub_id_j = cJSON_GetObjectItem(root, "hub_id");
    cJSON *token_j  = cJSON_GetObjectItem(root, "token");
    cJSON *code_j   = cJSON_GetObjectItem(root, "pairing_code");

    if (!cJSON_IsString(hub_id_j) || !cJSON_IsString(token_j)) {
        LOG_ERROR("provision", "Response missing hub_id or token");
        cJSON_Delete(root);
        return -1;
    }

    // Обновляем конфиг
    strncpy(cfg->hub_id,    hub_id_j->valuestring, sizeof(cfg->hub_id)    - 1);
    strncpy(cfg->hub_token, token_j->valuestring,  sizeof(cfg->hub_token) - 1);

    LOG_INFO("provision", "✓ Registered successfully");
    LOG_INFO("provision", "  hub_id:    %s", cfg->hub_id);
    LOG_INFO("provision", "  token:     (set, %zu chars)", strlen(cfg->hub_token));

    // Показываем pairing-код на консоли/дисплее
    if (cJSON_IsString(code_j)) {
        printf("\n");
        printf("╔══════════════════════════════╗\n");
        printf("║  PAIRING CODE: %-8s      ║\n", code_j->valuestring);
        printf("║  Enter in mobile app         ║\n");
        printf("╚══════════════════════════════╝\n\n");
        LOG_INFO("provision", "Pairing code: %s", code_j->valuestring);
    }

    cJSON_Delete(root);

    // Сохраняем обновлённый конфиг
    if (config_save(cfg, config_path) < 0) {
        LOG_ERROR("provision", "Failed to save config — credentials NOT persisted!");
        // Не возвращаем -1 — credentials в памяти есть, работаем
    }

    LOG_INFO("provision", "Provisioning complete");
    return 0;
}
