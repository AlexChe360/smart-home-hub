#include "hub_core.h"
#include "provisioning.h"
#include "message.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Helpers ── */

/* Send string via WS and free it */
static void ws_send_free(ws_client_t *ws, char *msg)
{
    if (msg) {
        ws_send(ws, msg, strlen(msg));
        free(msg);
    }
}

/* Strip base topic prefix, return remainder or NULL */
static const char *strip_base(const char *topic, const char *base)
{
    size_t blen = strlen(base);
    if (strncmp(topic, base, blen) != 0 || topic[blen] != '/')
        return NULL;
    return topic + blen + 1;
}

/* ── Forward declarations ── */
static void on_mqtt_message(const char *topic, const char *payload,
                            int payload_len, void *userdata);
static void on_mqtt_connect(bool connected, void *userdata);
static void on_ws_message(const char *message, size_t len, void *userdata);
static void on_ws_connect(bool connected, void *userdata);
static void handle_command(hub_core_t *hub, hub_message_t *msg);
static void send_status(hub_core_t *hub);

/* ── MQTT message handler ── */

static void on_mqtt_message(const char *topic, const char *payload,
                            int payload_len, void *userdata)
{
    hub_core_t *hub = (hub_core_t *)userdata;

    LOG_DEBUG("hub", "RAW MQTT: topic=%s len=%d", topic, payload_len);

    /* Null-terminate */
    char *buf = malloc(payload_len + 1);
    if (!buf) return;
    memcpy(buf, payload, payload_len);
    buf[payload_len] = '\0';

    const char *rem = strip_base(topic, hub->config.z2m_base_topic);
    if (!rem) { free(buf); return; }

    LOG_DEBUG("hub", "MQTT: %s", topic);

    /* Bridge messages */
    if (strncmp(rem, "bridge/", 7) == 0) {
        const char *sub = rem + 7;

        if (strcmp(sub, "state") == 0) {
            cJSON *root = cJSON_Parse(buf);
            if (root) {
                cJSON *s = cJSON_GetObjectItem(root, "state");
                if (cJSON_IsString(s))
                    LOG_INFO("hub", "Zigbee2MQTT: %s", s->valuestring);
                cJSON_Delete(root);
            }
        }
        else if (strcmp(sub, "devices") == 0) {
            devmgr_update_device_list(&hub->devmgr, buf);
        }
        else if (strcmp(sub, "event") == 0) {
            const char *event = devmgr_process_event(&hub->devmgr, buf);

            if (event && ws_is_connected(&hub->ws) && hub->devmgr.pair_mode &&
                (strcmp(event, "device_joined") == 0 ||
                 strcmp(event, "device_announce") == 0)) {

                cJSON *root = cJSON_Parse(buf);
                if (root) {
                    cJSON *data = cJSON_GetObjectItem(root, "data");
                    const char *ieee = "", *fname = "";
                    if (data) {
                        cJSON *v;
                        if ((v = cJSON_GetObjectItem(data, "ieee_address")) && cJSON_IsString(v))
                            ieee = v->valuestring;
                        if ((v = cJSON_GetObjectItem(data, "friendly_name")) && cJSON_IsString(v))
                            fname = v->valuestring;
                    }
                    device_t *dev = ieee[0] ? devmgr_find_by_ieee(&hub->devmgr, ieee) : NULL;
                    ws_send_free(&hub->ws,
                        msg_make_pair_result(hub->config.hub_id, true,
                            ieee, dev ? dev->model : "", fname));
                    cJSON_Delete(root);
                }
            }
        }
        else if (strncmp(sub, "response/", 9) == 0) {
            LOG_DEBUG("hub", "Bridge response: %s", sub + 9);
        }

        free(buf);
        return;
    }

    /* Device state — skip sub-topics (has '/') */
    if (strchr(rem, '/') != NULL) { free(buf); return; }

    device_t *dev = devmgr_update_state(&hub->devmgr, rem, buf);
    if (dev && ws_is_connected(&hub->ws)) {
        ws_send_free(&hub->ws,
            msg_make_telemetry(hub->config.hub_id,
                dev->friendly_name, dev->ieee, buf));
    }

    free(buf);
}

static void on_mqtt_connect(bool connected, void *userdata)
{
    hub_core_t *hub = (hub_core_t *)userdata;
    LOG_INFO("hub", "MQTT %s", connected ? "connected" : "disconnected");

    if (connected) {
        char topic[256];
        snprintf(topic, sizeof(topic), "%s/bridge/request/devices",
                 hub->config.z2m_base_topic);
        mqtt_publish(&hub->mqtt, topic, "");
    }
}

/* ── WebSocket message handler ── */

static void on_ws_message(const char *message, size_t len, void *userdata)
{
    hub_core_t *hub = (hub_core_t *)userdata;
    (void)len;

    LOG_DEBUG("hub", "Cloud -> Hub: %.200s", message);

    hub_message_t msg;
    if (msg_parse(message, &msg) < 0) {
        LOG_WARN("hub", "Failed to parse cloud message");
        return;
    }

    switch (msg.type) {

    case MSG_COMMAND:
        handle_command(hub, &msg);
        break;

    case MSG_PAIR_START: {
        int dur = 60;
        cJSON *v = cJSON_GetObjectItem(msg.payload, "duration");
        if (cJSON_IsNumber(v)) dur = (int)v->valuedouble;
        devmgr_set_pair_mode(&hub->devmgr, true, dur);
        mqtt_permit_join(&hub->mqtt, dur);
        ws_send_free(&hub->ws, msg_make_ack(hub->config.hub_id, msg.id, "ok"));
        break;
    }

    case MSG_PAIR_STOP:
        devmgr_set_pair_mode(&hub->devmgr, false, 0);
        mqtt_permit_join(&hub->mqtt, 0);
        ws_send_free(&hub->ws, msg_make_ack(hub->config.hub_id, msg.id, "ok"));
        break;

    case MSG_RENAME: {
        cJSON *ieee_j = cJSON_GetObjectItem(msg.payload, "ieee");
        cJSON *name_j = cJSON_GetObjectItem(msg.payload, "friendly_name");
        if (cJSON_IsString(ieee_j) && cJSON_IsString(name_j)) {
            device_t *dev = devmgr_find_by_ieee(&hub->devmgr, ieee_j->valuestring);
            if (dev) {
                mqtt_rename_device(&hub->mqtt, dev->friendly_name, name_j->valuestring);
                strncpy(dev->friendly_name, name_j->valuestring, DM_MAX_NAME - 1);
            }
        }
        ws_send_free(&hub->ws, msg_make_ack(hub->config.hub_id, msg.id, "ok"));
        break;
    }

    case MSG_REMOVE: {
        cJSON *ieee_j = cJSON_GetObjectItem(msg.payload, "ieee");
        if (cJSON_IsString(ieee_j))
            mqtt_remove_device(&hub->mqtt, ieee_j->valuestring);
        ws_send_free(&hub->ws, msg_make_ack(hub->config.hub_id, msg.id, "ok"));
        break;
    }

    case MSG_PONG:
        hub->ws.last_pong_time = time(NULL);
        break;

    default:
        LOG_WARN("hub", "Unknown cloud msg type: %s", msg_type_str(msg.type));
        break;
    }

    msg_free(&msg);
}

static void handle_command(hub_core_t *hub, hub_message_t *msg)
{
    cJSON *ieee_j = cJSON_GetObjectItem(msg->payload, "ieee");
    cJSON *data_j = cJSON_GetObjectItem(msg->payload, "data");

    if (!cJSON_IsString(ieee_j) || !data_j) {
        LOG_WARN("hub", "Invalid command: missing ieee/data");
        return;
    }

    device_t *dev = devmgr_find_by_ieee(&hub->devmgr, ieee_j->valuestring);
    if (!dev) {
        LOG_WARN("hub", "Command for unknown device: %s", ieee_j->valuestring);
        ws_send_free(&hub->ws,
            msg_make_ack(hub->config.hub_id, msg->id, "error:device_not_found"));
        return;
    }

    char *data_str = cJSON_PrintUnformatted(data_j);
    if (data_str) {
        mqtt_device_command(&hub->mqtt, dev->friendly_name, data_str);
        free(data_str);
    }

    ws_send_free(&hub->ws, msg_make_ack(hub->config.hub_id, msg->id, "ok"));
}

static void on_ws_connect(bool connected, void *userdata)
{
    hub_core_t *hub = (hub_core_t *)userdata;
    LOG_INFO("hub", "Cloud WebSocket %s", connected ? "connected" : "disconnected");
    if (connected) send_status(hub);
}

static void send_status(hub_core_t *hub)
{
    uint64_t uptime = (uint64_t)(time(NULL) - hub->start_time);
    ws_send_free(&hub->ws,
        msg_make_status(hub->config.hub_id, uptime,
            devmgr_count(&hub->devmgr),
            hub->mqtt.connected ? "online" : "offline"));
    hub->last_status_report = time(NULL);
}

/* ── Public API ── */

int hub_init(hub_core_t *hub, const char *config_path)
{
    memset(hub, 0, sizeof(*hub));
 
    // Загружаем конфиг
    if (config_load(&hub->config, config_path) < 0)
        return -1;
 
    logger_set_level(hub->config.log_level);
    config_dump(&hub->config);
 
    // ── Zero-touch provisioning ───────────────────────────────────────────────
    // Если токен не настроен — это первый запуск.
    // Автоматически регистрируемся в облаке и получаем credentials.
    if (config_needs_provisioning(&hub->config)) {
        LOG_INFO("hub", "First run detected — starting provisioning...");
 
        // Повторяем попытки каждые 10 секунд пока не получится
        // (хаб мог стартовать раньше чем появилась сеть)
        int attempts = 0;
        int max_attempts = 12; // 2 минуты
        int ok = -1;
 
        while (ok < 0 && attempts < max_attempts) {
            if (attempts > 0) {
                LOG_INFO("hub", "Retrying provisioning in 10s... (%d/%d)",
                         attempts + 1, max_attempts);
                sleep(10);
            }
            ok = hub_provision(&hub->config, config_path);
            attempts++;
        }
 
        if (ok < 0) {
            LOG_ERROR("hub", "Provisioning failed after %d attempts", max_attempts);
            return -1;
        }
    }
    // ── Конец блока провижинга ────────────────────────────────────────────────
 
    devmgr_init(&hub->devmgr);
 
    if (mqtt_init(&hub->mqtt,
                  hub->config.mqtt_host,
                  hub->config.mqtt_port,
                  hub->config.mqtt_client_id,
                  hub->config.z2m_base_topic) < 0) {
        LOG_ERROR("hub", "Failed to init MQTT");
        return -1;
    }
    mqtt_set_callbacks(&hub->mqtt, on_mqtt_message, on_mqtt_connect, hub);
 
    if (ws_init(&hub->ws,
                hub->config.cloud_host,
                hub->config.cloud_port,
                hub->config.cloud_path,
                hub->config.cloud_use_tls,
                hub->config.hub_id,
                hub->config.hub_token) < 0) {
        LOG_ERROR("hub", "Failed to init WebSocket");
        return -1;
    }
    ws_set_callbacks(&hub->ws, on_ws_message, on_ws_connect, hub);
    ws_set_reconnect(&hub->ws, hub->config.reconnect_min, hub->config.reconnect_max);
    ws_set_heartbeat(&hub->ws, hub->config.heartbeat_interval);
 
    hub->start_time = time(NULL);
    hub->running = true;
 
    return 0;
}

int hub_run(hub_core_t *hub)
{
    LOG_INFO("hub", "Starting hub %s", hub->config.hub_id);

    if (mqtt_connect(&hub->mqtt) < 0) {
        LOG_ERROR("hub", "Failed to connect MQTT");
        return -1;
    }
    if (ws_connect(&hub->ws) < 0) {
        LOG_ERROR("hub", "Failed to initiate WebSocket");
        return -1;
    }

    /* Main event loop */
    while (hub->running) {
        /* MQTT: non-blocking, process all pending messages */
        mqtt_loop(&hub->mqtt, 0);

        /* WebSocket: non-blocking */
        ws_service(&hub->ws, 0);

        /* Small sleep to avoid busy-spinning (1ms) */
        struct timespec ts = {0, 1000000}; /* 1ms */
        nanosleep(&ts, NULL);

        time_t now = time(NULL);

        /* Periodic status */
        if (now - hub->last_status_report >= hub->config.status_report_interval) {
            if (ws_is_connected(&hub->ws))
                send_status(hub);
        }

        /* Pair mode expiry */
        if (hub->devmgr.pair_mode && devmgr_pair_expired(&hub->devmgr)) {
            devmgr_set_pair_mode(&hub->devmgr, false, 0);
            mqtt_permit_join(&hub->mqtt, 0);
            LOG_INFO("hub", "Pair mode expired");
        }

        /* Application-level ping */
        if (ws_is_connected(&hub->ws) &&
            now - hub->last_app_ping >= hub->config.heartbeat_interval) {
            ws_send_free(&hub->ws, msg_make_ping(hub->config.hub_id));
            hub->last_app_ping = now;
        }
    }

    LOG_INFO("hub", "Hub stopping...");
    return 0;
}

void hub_stop(hub_core_t *hub)
{
    hub->running = false;
}

void hub_destroy(hub_core_t *hub)
{
    mqtt_destroy(&hub->mqtt);
    ws_destroy(&hub->ws);
    LOG_INFO("hub", "Hub destroyed");
}
