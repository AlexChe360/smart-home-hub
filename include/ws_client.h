//
//  ws_client.h
//  smart_home
//
//  Created by  Alexey on 12.03.2026.
//

#ifndef WS_CLIENT_H
#define WS_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include <libwebsockets.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ws_msg_cb)(const char *message, size_t len, void *userdata);
typedef void (*ws_conn_cb)(bool connected, void *userdata);

#define WS_TX_BUF_SIZE  65536
#define WS_TX_QUEUE_CAP 256

typedef struct {
    char   data[WS_TX_BUF_SIZE];
    size_t len;
} ws_tx_item_t;

typedef struct {
    /* LWS */
    struct lws_context *ctx;
    struct lws         *wsi;

    /* Connection */
    char    host[256];
    int     port;
    char    path[128];
    bool    use_tls;
    char    hub_id[32];
    char    hub_token[128];

    /* State */
    bool    connected;
    bool    connecting;
    bool    should_exit;

    /* Reconnect */
    int     reconnect_delay;
    int     reconnect_min;
    int     reconnect_max;
    time_t  next_reconnect;

    /* Heartbeat */
    int     heartbeat_interval;
    time_t  last_ping_time;
    time_t  last_pong_time;

    /* TX ring buffer */
    ws_tx_item_t tx_queue[WS_TX_QUEUE_CAP];
    int     tx_head;
    int     tx_tail;
    int     tx_count;

    /* RX accumulation */
    char    rx_buf[WS_TX_BUF_SIZE];
    size_t  rx_len;

    /* Callbacks */
    ws_msg_cb  on_message;
    ws_conn_cb on_connect;
    void      *userdata;
} ws_client_t;

int  ws_init(ws_client_t *c, const char *host, int port,
             const char *path, bool use_tls,
             const char *hub_id, const char *hub_token);
void ws_set_callbacks(ws_client_t *c, ws_msg_cb on_msg,
                      ws_conn_cb on_conn, void *userdata);
void ws_set_reconnect(ws_client_t *c, int min_sec, int max_sec);
void ws_set_heartbeat(ws_client_t *c, int interval_sec);
int  ws_connect(ws_client_t *c);
int  ws_send(ws_client_t *c, const char *message, size_t len);
int  ws_service(ws_client_t *c, int timeout_ms);
void ws_destroy(ws_client_t *c);
bool ws_is_connected(const ws_client_t *c);

#ifdef __cplusplus
}
#endif

#endif /* WS_CLIENT_H */
