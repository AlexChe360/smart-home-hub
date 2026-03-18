#include "config.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void config_defaults(hub_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    strncpy(cfg->mqtt_host, "127.0.0.1", sizeof(cfg->mqtt_host) - 1);
    cfg->mqtt_port = 1883;
    strncpy(cfg->mqtt_client_id, "smarthome-hub", sizeof(cfg->mqtt_client_id) - 1);

    strncpy(cfg->cloud_host, "api.smarthome.com", sizeof(cfg->cloud_host) - 1);
    cfg->cloud_port = 443;
    strncpy(cfg->cloud_path, "/ws/hub", sizeof(cfg->cloud_path) - 1);
    cfg->cloud_use_tls = true;

    strncpy(cfg->z2m_base_topic, "zigbee2mqtt", sizeof(cfg->z2m_base_topic) - 1);

    cfg->heartbeat_interval    = 30;
    cfg->reconnect_min         = 1;
    cfg->reconnect_max         = 60;
    cfg->status_report_interval = 300;

    cfg->log_level = LOG_LVL_INFO;
    strncpy(cfg->log_file, "/var/log/smarthome-hub.log", sizeof(cfg->log_file) - 1);
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
    return s;
}

static void unquote(char *s)
{
    size_t len = strlen(s);
    if (len >= 2 &&
        ((s[0] == '"'  && s[len-1] == '"') ||
         (s[0] == '\'' && s[len-1] == '\''))) {
        memmove(s, s + 1, len - 2);
        s[len - 2] = '\0';
    }
}

/* Macro to reduce repetition */
#define SET_STR(field) strncpy(cfg->field, val, sizeof(cfg->field) - 1)
#define SET_INT(field) cfg->field = atoi(val)
#define SET_BOOL(field) cfg->field = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0)

static void config_set(hub_config_t *cfg, const char *key, const char *val)
{
    if      (strcmp(key, "hub_id") == 0)                SET_STR(hub_id);
    else if (strcmp(key, "hub_token") == 0)             SET_STR(hub_token);
    else if (strcmp(key, "mqtt_host") == 0)             SET_STR(mqtt_host);
    else if (strcmp(key, "mqtt_port") == 0)             SET_INT(mqtt_port);
    else if (strcmp(key, "mqtt_client_id") == 0)        SET_STR(mqtt_client_id);
    else if (strcmp(key, "cloud_host") == 0)            SET_STR(cloud_host);
    else if (strcmp(key, "cloud_port") == 0)            SET_INT(cloud_port);
    else if (strcmp(key, "cloud_path") == 0)            SET_STR(cloud_path);
    else if (strcmp(key, "cloud_use_tls") == 0)         SET_BOOL(cloud_use_tls);
    else if (strcmp(key, "z2m_base_topic") == 0)        SET_STR(z2m_base_topic);
    else if (strcmp(key, "heartbeat_interval") == 0)    SET_INT(heartbeat_interval);
    else if (strcmp(key, "reconnect_min") == 0)         SET_INT(reconnect_min);
    else if (strcmp(key, "reconnect_max") == 0)         SET_INT(reconnect_max);
    else if (strcmp(key, "status_report_interval") == 0) SET_INT(status_report_interval);
    else if (strcmp(key, "log_level") == 0)             SET_INT(log_level);
    else if (strcmp(key, "log_file") == 0)              SET_STR(log_file);
    else LOG_WARN("config", "Unknown key: %s", key);
}

int config_load(hub_config_t *cfg, const char *filepath)
{
    config_defaults(cfg);

    FILE *f = fopen(filepath, "r");
    if (!f) {
        LOG_ERROR("config", "Cannot open %s", filepath);
        return -1;
    }

    char line[1024];
    int lineno = 0;

    while (fgets(line, sizeof(line), f)) {
        lineno++;
        char *s = trim(line);

        if (*s == '\0' || *s == '#' || *s == ';')
            continue;

        char *eq = strchr(s, '=');
        if (!eq) {
            LOG_WARN("config", "Syntax error at line %d", lineno);
            continue;
        }

        *eq = '\0';
        char *key = trim(s);
        char *val = trim(eq + 1);
        unquote(val);

        config_set(cfg, key, val);
    }

    fclose(f);

    if (cfg->hub_id[0] == '\0') {
        LOG_ERROR("config", "hub_id is required");
        return -1;
    }
    if (cfg->hub_token[0] == '\0') {
        LOG_ERROR("config", "hub_token is required");
        return -1;
    }

    return 0;
}

void config_dump(const hub_config_t *cfg)
{
    LOG_INFO("config", "=== Hub Configuration ===");
    LOG_INFO("config", "hub_id:       %s", cfg->hub_id);
    LOG_INFO("config", "hub_token:    %s", cfg->hub_token[0] ? "(set)" : "(empty)");
    LOG_INFO("config", "mqtt:         %s:%d", cfg->mqtt_host, cfg->mqtt_port);
    LOG_INFO("config", "cloud:        %s://%s:%d%s",
             cfg->cloud_use_tls ? "wss" : "ws",
             cfg->cloud_host, cfg->cloud_port, cfg->cloud_path);
    LOG_INFO("config", "z2m_topic:    %s", cfg->z2m_base_topic);
    LOG_INFO("config", "heartbeat:    %ds", cfg->heartbeat_interval);
    LOG_INFO("config", "reconnect:    %d-%ds", cfg->reconnect_min, cfg->reconnect_max);
    LOG_INFO("config", "log_level:    %d", cfg->log_level);
    LOG_INFO("config", "=========================");
}
