//
//  mqtt_client.h
//  smart_home
//
//  Created by  Alexey on 12.03.2026.
//

#ifndef mqtt_client_h
#define mqtt_client_h

#include <stdbool.h>
#include <mosquitto.h>

#ifdef __cplusplus
extern "C" {
#endif

    typedef void (*mqtt_msg_cb)(const char *topic, const char *payload,
                                int payload_len, void *userdata);
    typedef void (*mqtt_conn_cb)(bool connected, void *userdata);

    typedef struct {
        struct mosquitto *mosq;
        char host[64];
        int port;
        char client_id[64];
        char base_topic[64];
        bool connected;
        mqtt_msg_cb on_message;
        mqtt_conn_cb on_connect;
        void *userdata;
    } mqtt_client_t;

    int mqtt_init(mqtt_client_t *c, const char *host, int port,
                  const char *client_id, const char *base_topic);
    void mqtt_set_callbacks(mqtt_client_t *c, mqtt_msg_cb on_msg,
                            mqtt_conn_cb on_conn, void *userdata);
    int mqtt_connect(mqtt_client_t *c);
    int mqtt_publish(mqtt_client_t *c, const char *topic, const char *payload);
    int mqtt_loop(mqtt_client_t *c, int timeout_ms);
    void mqtt_destroy(mqtt_client_t *c);

    /* Zigbee2MQTT commands */
    int mqtt_permit_join(mqtt_client_t *c, int duration_sec);
    int mqtt_rename_device(mqtt_client_t *c, const char *old_name, const char *new_name);
    int mqtt_remove_device(mqtt_client_t *c, const char *ieee);
    int mqtt_device_command(mqtt_client_t *c, const char *frendly_name, const char *payload_json);

#ifdef __cplusplus
}
#endif

#endif // mqtt_client_h
