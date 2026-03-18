//
//  config.h — дополненная версия
//

#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// Значение hub_token при первом запуске — триггер для автопровиженинга
#define PROVISIONING_TOKEN "change-me-during-setup"
// Значение hub_id при первом запуске — серийный номер устройства
// После провижинга заменяется на UUID из облака
#define PROVISIONING_HUB_ID_PREFIX "HUB-"

typedef struct {
    /* Hub identity */
    char hub_id[64];       // до провижинга: серийный номер, после: UUID
    char hub_token[512];   // до провижинга: PROVISIONING_TOKEN, после: JWT

    /* MQTT (local Mosquitto) */
    char mqtt_host[64];
    int  mqtt_port;
    char mqtt_client_id[64];

    /* Cloud WebSocket */
    char cloud_host[256];
    int  cloud_port;
    char cloud_path[128];
    bool cloud_use_tls;

    /* Cloud REST (для провижинга) */
    char cloud_api_url[256];  // https://api.smarthome.com

    /* Zigbee2MQTT topics */
    char z2m_base_topic[64];

    /* Timings (seconds) */
    int heartbeat_interval;
    int reconnect_min;
    int reconnect_max;
    int status_report_interval;

    /* Logging */
    int  log_level;
    char log_file[256];
} hub_config_t;

int  config_load  (hub_config_t *cfg, const char *filepath);
int  config_save  (const hub_config_t *cfg, const char *filepath);
void config_defaults(hub_config_t *cfg);
void config_dump  (const hub_config_t *cfg);

// Удобная проверка — нужен ли провижинг
static inline bool config_needs_provisioning(const hub_config_t *cfg)
{
    return strcmp(cfg->hub_token, PROVISIONING_TOKEN) == 0;
}

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_H */
