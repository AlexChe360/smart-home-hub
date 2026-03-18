#include "mqtt_client.h"
#include "logger.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Mosquitto callbacks ── */

static void cb_connect(struct mosquitto *mosq, void *obj, int rc)
{
    mqtt_client_t *c = (mqtt_client_t *)obj;

    if (rc == 0) {
        LOG_INFO("mqtt", "Connected to %s:%d", c->host, c->port);
        c->connected = true;

        char topic[256];
        snprintf(topic, sizeof(topic), "%s/+", c->base_topic);
        mosquitto_subscribe(mosq, NULL, topic, 0);

        snprintf(topic, sizeof(topic), "%s/bridge/#", c->base_topic);
        mosquitto_subscribe(mosq, NULL, topic, 0);

        LOG_DEBUG("mqtt", "Subscribed to %s/+ and %s/bridge/#",
                  c->base_topic, c->base_topic);

        if (c->on_connect) c->on_connect(true, c->userdata);
    } else {
        LOG_ERROR("mqtt", "Connection failed: rc=%d", rc);
        c->connected = false;
        if (c->on_connect) c->on_connect(false, c->userdata);
    }
}

static void cb_disconnect(struct mosquitto *mosq, void *obj, int rc)
{
    (void)mosq;
    mqtt_client_t *c = (mqtt_client_t *)obj;
    c->connected = false;
    LOG_WARN("mqtt", "Disconnected (rc=%d)", rc);
    if (c->on_connect) c->on_connect(false, c->userdata);
}

static void cb_message(struct mosquitto *mosq, void *obj,
                       const struct mosquitto_message *msg)
{
    (void)mosq;
    mqtt_client_t *c = (mqtt_client_t *)obj;

    LOG_DEBUG("mqtt", "cb_message: topic=%s payloadlen=%d", msg->topic, msg->payloadlen);

    if (!msg->payload || msg->payloadlen == 0) return;

    if (c->on_message)
        c->on_message(msg->topic, (const char *)msg->payload,
                      msg->payloadlen, c->userdata);
}

/* ── Public API ── */

int mqtt_init(mqtt_client_t *c, const char *host, int port,
              const char *client_id, const char *base_topic)
{
    memset(c, 0, sizeof(*c));
    strncpy(c->host, host, sizeof(c->host) - 1);
    c->port = port;
    strncpy(c->client_id, client_id, sizeof(c->client_id) - 1);
    strncpy(c->base_topic, base_topic, sizeof(c->base_topic) - 1);

    mosquitto_lib_init();

    c->mosq = mosquitto_new(client_id, true, c);
    if (!c->mosq) {
        LOG_ERROR("mqtt", "Failed to create mosquitto instance");
        return -1;
    }

    mosquitto_connect_callback_set(c->mosq, cb_connect);
    mosquitto_disconnect_callback_set(c->mosq, cb_disconnect);
    mosquitto_message_callback_set(c->mosq, cb_message);
    mosquitto_reconnect_delay_set(c->mosq, 1, 10, true);

    return 0;
}

void mqtt_set_callbacks(mqtt_client_t *c, mqtt_msg_cb on_msg,
                        mqtt_conn_cb on_conn, void *userdata)
{
    c->on_message = on_msg;
    c->on_connect = on_conn;
    c->userdata = userdata;
}

int mqtt_connect(mqtt_client_t *c)
{
    LOG_INFO("mqtt", "Connecting to %s:%d...", c->host, c->port);
    int rc = mosquitto_connect_async(c->mosq, c->host, c->port, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        LOG_ERROR("mqtt", "Connect failed: %s", mosquitto_strerror(rc));
        return -1;
    }
    return 0;
}

int mqtt_publish(mqtt_client_t *c, const char *topic, const char *payload)
{
    if (!c->connected) {
        LOG_WARN("mqtt", "Cannot publish — not connected");
        return -1;
    }
    int rc = mosquitto_publish(c->mosq, NULL, topic,
                               (int)strlen(payload), payload, 1, false);
    if (rc != MOSQ_ERR_SUCCESS) {
        LOG_ERROR("mqtt", "Publish to %s failed: %s", topic, mosquitto_strerror(rc));
        return -1;
    }
    return 0;
}

int mqtt_loop(mqtt_client_t *c, int timeout_ms)
{
    if (!c->mosq) return -1;
    int rc = mosquitto_loop(c->mosq, timeout_ms, 1);
    if (rc == MOSQ_ERR_CONN_LOST || rc == MOSQ_ERR_NO_CONN)
        mosquitto_reconnect_async(c->mosq);
    return rc;
}

void mqtt_destroy(mqtt_client_t *c)
{
    if (c->mosq) {
        mosquitto_disconnect(c->mosq);
        mosquitto_destroy(c->mosq);
        c->mosq = NULL;
    }
    mosquitto_lib_cleanup();
}

/* ── Zigbee2MQTT commands ── */

int mqtt_permit_join(mqtt_client_t *c, int duration_sec)
{
    char topic[256];
    snprintf(topic, sizeof(topic), "%s/bridge/request/permit_join", c->base_topic);

    cJSON *p = cJSON_CreateObject();
    cJSON_AddNumberToObject(p, "value", duration_sec > 0 ? duration_sec : 0);
    char *json = cJSON_PrintUnformatted(p);
    cJSON_Delete(p);

    LOG_INFO("mqtt", "Permit join: %d sec", duration_sec);
    int rc = mqtt_publish(c, topic, json);
    free(json);
    return rc;
}

int mqtt_rename_device(mqtt_client_t *c, const char *old_name, const char *new_name)
{
    char topic[256];
    snprintf(topic, sizeof(topic), "%s/bridge/request/device/rename", c->base_topic);

    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "from", old_name);
    cJSON_AddStringToObject(p, "to", new_name);
    char *json = cJSON_PrintUnformatted(p);
    cJSON_Delete(p);

    LOG_INFO("mqtt", "Rename: %s -> %s", old_name, new_name);
    int rc = mqtt_publish(c, topic, json);
    free(json);
    return rc;
}

int mqtt_remove_device(mqtt_client_t *c, const char *ieee)
{
    char topic[256];
    snprintf(topic, sizeof(topic), "%s/bridge/request/device/remove", c->base_topic);

    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "id", ieee);
    char *json = cJSON_PrintUnformatted(p);
    cJSON_Delete(p);

    LOG_INFO("mqtt", "Remove: %s", ieee);
    int rc = mqtt_publish(c, topic, json);
    free(json);
    return rc;
}

int mqtt_device_command(mqtt_client_t *c, const char *friendly_name,
                        const char *payload_json)
{
    char topic[256];
    snprintf(topic, sizeof(topic), "%s/%s/set", c->base_topic, friendly_name);
    LOG_DEBUG("mqtt", "Command to %s: %s", friendly_name, payload_json);
    return mqtt_publish(c, topic, payload_json);
}
