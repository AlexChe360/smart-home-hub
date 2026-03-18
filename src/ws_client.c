#include "ws_client.h"
#include "logger.h"
#include <string.h>
#include <stdlib.h>

/* ── TX ring buffer ── */

static int tx_enqueue(ws_client_t *c, const char *data, size_t len)
{
    if (c->tx_count >= WS_TX_QUEUE_CAP) {
        LOG_WARN("ws", "TX queue full, dropping");
        return -1;
    }
    if (len >= WS_TX_BUF_SIZE) {
        LOG_WARN("ws", "Message too large (%zu)", len);
        return -1;
    }
    ws_tx_item_t *slot = &c->tx_queue[c->tx_head];
    memcpy(slot->data, data, len);
    slot->data[len] = '\0';
    slot->len = len;
    c->tx_head = (c->tx_head + 1) % WS_TX_QUEUE_CAP;
    c->tx_count++;
    return 0;
}

static ws_tx_item_t *tx_peek(ws_client_t *c)
{
    return c->tx_count > 0 ? &c->tx_queue[c->tx_tail] : NULL;
}

static void tx_dequeue(ws_client_t *c)
{
    if (c->tx_count > 0) {
        c->tx_tail = (c->tx_tail + 1) % WS_TX_QUEUE_CAP;
        c->tx_count--;
    }
}

/* ── Build auth path ── */

static void build_path(const ws_client_t *c, char *buf, size_t buflen)
{
    snprintf(buf, buflen, "%s?hub_id=%s&token=%s", c->path, c->hub_id, c->hub_token);
}

/* ── Schedule reconnect with exponential backoff ── */

static void schedule_reconnect(ws_client_t *c)
{
    c->next_reconnect = time(NULL) + c->reconnect_delay;
    LOG_INFO("ws", "Reconnect in %d sec", c->reconnect_delay);
    c->reconnect_delay *= 2;
    if (c->reconnect_delay > c->reconnect_max)
        c->reconnect_delay = c->reconnect_max;
}

/* ── LWS callback ── */

static int lws_callback(struct lws *wsi, enum lws_callback_reasons reason,
                        void *user, void *in, size_t len)
{
    (void)user;
    ws_client_t *c = (ws_client_t *)lws_context_user(lws_get_context(wsi));

    switch (reason) {

    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        LOG_INFO("ws", "Connected to %s:%d%s", c->host, c->port, c->path);
        c->connected = true;
        c->connecting = false;
        c->reconnect_delay = c->reconnect_min;
        c->last_pong_time = time(NULL);
        if (c->on_connect) c->on_connect(true, c->userdata);
        lws_callback_on_writable(wsi);
        break;

    case LWS_CALLBACK_CLIENT_RECEIVE:
        if (!in || len == 0) break;

        if (c->rx_len + len >= sizeof(c->rx_buf)) {
            LOG_WARN("ws", "RX buffer overflow");
            c->rx_len = 0;
            break;
        }
        memcpy(c->rx_buf + c->rx_len, in, len);
        c->rx_len += len;

        if (lws_is_final_fragment(wsi) && lws_remaining_packet_payload(wsi) == 0) {
            c->rx_buf[c->rx_len] = '\0';
            if (c->on_message)
                c->on_message(c->rx_buf, c->rx_len, c->userdata);
            c->rx_len = 0;
        }
        break;

    case LWS_CALLBACK_CLIENT_WRITEABLE: {
        ws_tx_item_t *item = tx_peek(c);
        if (item) {
            unsigned char *buf = malloc(LWS_PRE + item->len);
            if (buf) {
                memcpy(buf + LWS_PRE, item->data, item->len);
                int written = lws_write(wsi, buf + LWS_PRE, item->len, LWS_WRITE_TEXT);
                free(buf);
                if (written < 0) {
                    LOG_ERROR("ws", "Write failed");
                    break;
                }
            }
            tx_dequeue(c);
            if (c->tx_count > 0) lws_callback_on_writable(wsi);
        }
        break;
    }

    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        LOG_WARN("ws", "Connection error: %s", in ? (const char *)in : "unknown");
        c->connected = false;
        c->connecting = false;
        c->wsi = NULL;
        if (c->on_connect) c->on_connect(false, c->userdata);
        schedule_reconnect(c);
        break;

    case LWS_CALLBACK_CLIENT_CLOSED:
        LOG_WARN("ws", "Connection closed");
        c->connected = false;
        c->connecting = false;
        c->wsi = NULL;
        if (c->on_connect) c->on_connect(false, c->userdata);
        schedule_reconnect(c);
        break;

    case LWS_CALLBACK_CLIENT_RECEIVE_PONG:
        c->last_pong_time = time(NULL);
        break;

    default:
        break;
    }
    return 0;
}

static const struct lws_protocols protocols[] = {
    { "smarthome-hub", lws_callback, 0, WS_TX_BUF_SIZE, 0, NULL, 0 },
    { NULL, NULL, 0, 0, 0, NULL, 0 }
};

/* ── Public API ── */

int ws_init(ws_client_t *c, const char *host, int port,
            const char *path, bool use_tls,
            const char *hub_id, const char *hub_token)
{
    memset(c, 0, sizeof(*c));
    strncpy(c->host, host, sizeof(c->host) - 1);
    c->port = port;
    strncpy(c->path, path, sizeof(c->path) - 1);
    c->use_tls = use_tls;
    strncpy(c->hub_id, hub_id, sizeof(c->hub_id) - 1);
    strncpy(c->hub_token, hub_token, sizeof(c->hub_token) - 1);

    c->reconnect_min = 1;
    c->reconnect_max = 60;
    c->reconnect_delay = 1;
    c->heartbeat_interval = 30;
    return 0;
}

void ws_set_callbacks(ws_client_t *c, ws_msg_cb on_msg,
                      ws_conn_cb on_conn, void *userdata)
{
    c->on_message = on_msg;
    c->on_connect = on_conn;
    c->userdata = userdata;
}

void ws_set_reconnect(ws_client_t *c, int min_sec, int max_sec)
{
    c->reconnect_min = min_sec;
    c->reconnect_max = max_sec;
    c->reconnect_delay = min_sec;
}

void ws_set_heartbeat(ws_client_t *c, int interval_sec)
{
    c->heartbeat_interval = interval_sec;
}

static struct lws *try_connect(ws_client_t *c)
{
    struct lws_client_connect_info ci;
    memset(&ci, 0, sizeof(ci));

    char full_path[512];
    build_path(c, full_path, sizeof(full_path));

    ci.context  = c->ctx;
    ci.address  = c->host;
    ci.port     = c->port;
    ci.path     = full_path;
    ci.host     = c->host;
    ci.origin   = c->host;
    ci.protocol = protocols[0].name;
    ci.ssl_connection = c->use_tls
        ? (LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK)
        : 0;

    return lws_client_connect_via_info(&ci);
}

int ws_connect(ws_client_t *c)
{
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.user = c;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    c->ctx = lws_create_context(&info);
    if (!c->ctx) {
        LOG_ERROR("ws", "Failed to create LWS context");
        return -1;
    }

    c->connecting = true;
    c->wsi = try_connect(c);
    if (!c->wsi) {
        LOG_ERROR("ws", "Failed to initiate connection");
        c->connecting = false;
        return -1;
    }

    LOG_INFO("ws", "Connecting to %s://%s:%d%s...",
             c->use_tls ? "wss" : "ws", c->host, c->port, c->path);
    return 0;
}

int ws_send(ws_client_t *c, const char *message, size_t len)
{
    if (tx_enqueue(c, message, len) < 0) return -1;
    if (c->wsi && c->connected)
        lws_callback_on_writable(c->wsi);
    return 0;
}

int ws_service(ws_client_t *c, int timeout_ms)
{
    if (!c->ctx) return -1;

    time_t now = time(NULL);

    /* Reconnect */
    if (!c->connected && !c->connecting && !c->should_exit) {
        if (now >= c->next_reconnect) {
            LOG_INFO("ws", "Attempting reconnect...");
            c->connecting = true;
            c->wsi = try_connect(c);
            if (!c->wsi) {
                c->connecting = false;
                c->next_reconnect = now + c->reconnect_delay;
            }
        }
    }

    /* Heartbeat */
    if (c->connected && c->heartbeat_interval > 0) {
        if (now - c->last_ping_time >= c->heartbeat_interval) {
            if (c->wsi) {
                unsigned char buf[LWS_PRE + 1];
                lws_write(c->wsi, buf + LWS_PRE, 0, LWS_WRITE_PING);
                c->last_ping_time = now;
            }
        }
        /* Pong timeout: 3x heartbeat */
        if (now - c->last_pong_time > c->heartbeat_interval * 3) {
            LOG_WARN("ws", "Pong timeout — forcing reconnect");
            if (c->wsi) lws_set_timeout(c->wsi, PENDING_TIMEOUT_CLOSE_SEND, 1);
        }
    }

    return lws_service(c->ctx, timeout_ms);
}

void ws_destroy(ws_client_t *c)
{
    c->should_exit = true;
    if (c->ctx) {
        lws_context_destroy(c->ctx);
        c->ctx = NULL;
        c->wsi = NULL;
    }
}

bool ws_is_connected(const ws_client_t *c)
{
    return c->connected;
}
