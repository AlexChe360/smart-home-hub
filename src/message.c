//
//  message.c
//  smart_home
//
//  Created by  Alexey on 12.03.2026.
//

#include "message.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <uuid/uuid.h>

/* ── Type mapping ── */

static const struct { msg_type_t t; const char *s; } type_map[] = {
    { MSG_TELEMETRY,   "telemetry"   },
    { MSG_COMMAND,     "command"     },
    { MSG_ACK,         "ack"         },
    { MSG_PAIR_START,  "pair_start"  },
    { MSG_PAIR_STOP,   "pair_stop"   },
    { MSG_PAIR_RESULT, "pair_result" },
    { MSG_STATUS,      "status"      },
    { MSG_RENAME,      "rename"      },
    { MSG_REMOVE,      "remove"      },
    { MSG_OTA_UPDATE,  "ota_update"  },
    { MSG_PING,        "ping"        },
    { MSG_PONG,        "pong"        },
    { MSG_ERROR,       "error"       },
};
#define TYPE_MAP_N (sizeof(type_map) / sizeof(type_map[0]))

const char *msg_type_str(msg_type_t type)
{
    for (size_t i = 0; i < TYPE_MAP_N; i++)
        if (type_map[i].t == type) return type_map[i].s;
    return "unknown";
}

msg_type_t msg_type_from_str(const char *str)
{
    for (size_t i = 0; i < TYPE_MAP_N; i++)
        if (strcmp(type_map[i].s, str) == 0) return type_map[i].t;
    return MSG_UNKNOWN;
}

void msg_uuid(char *buf, size_t len)
{
    uuid_t u;
    uuid_generate(u);
    uuid_unparse_lower(u, buf);
    (void)len;
}

/* ── Parse / Serialize ── */

int msg_parse(const char *json_str, hub_message_t *msg)
{
    memset(msg, 0, sizeof(*msg));
    msg->type = MSG_UNKNOWN;

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        LOG_ERROR("msg", "JSON parse failed");
        return -1;
    }

    cJSON *v;
    if ((v = cJSON_GetObjectItem(root, "type")) && cJSON_IsString(v))
        msg->type = msg_type_from_str(v->valuestring);

    if ((v = cJSON_GetObjectItem(root, "id")) && cJSON_IsString(v))
        strncpy(msg->id, v->valuestring, sizeof(msg->id) - 1);

    if ((v = cJSON_GetObjectItem(root, "hub_id")) && cJSON_IsString(v))
        strncpy(msg->hub_id, v->valuestring, sizeof(msg->hub_id) - 1);

    if ((v = cJSON_GetObjectItem(root, "timestamp")) && cJSON_IsNumber(v))
        msg->timestamp = (uint64_t)v->valuedouble;

    v = cJSON_GetObjectItem(root, "payload");
    if (v) msg->payload = cJSON_Duplicate(v, 1);

    cJSON_Delete(root);
    return 0;
}

char *msg_serialize(const hub_message_t *msg)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", msg_type_str(msg->type));
    cJSON_AddStringToObject(root, "id", msg->id);
    cJSON_AddStringToObject(root, "hub_id", msg->hub_id);
    cJSON_AddNumberToObject(root, "timestamp", (double)msg->timestamp);
    if (msg->payload)
        cJSON_AddItemToObject(root, "payload", cJSON_Duplicate(msg->payload, 1));
    else
        cJSON_AddObjectToObject(root, "payload");

    char *str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return str;
}

void msg_free(hub_message_t *msg)
{
    if (msg->payload) {
        cJSON_Delete(msg->payload);
        msg->payload = NULL;
    }
}

/* ── Envelope builder ── */

static cJSON *make_envelope(const char *type_str, const char *hub_id, cJSON *payload)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", type_str);

    char uuid[40];
    msg_uuid(uuid, sizeof(uuid));
    cJSON_AddStringToObject(root, "id", uuid);
    cJSON_AddStringToObject(root, "hub_id", hub_id);
    cJSON_AddNumberToObject(root, "timestamp", (double)time(NULL));

    if (payload)
        cJSON_AddItemToObject(root, "payload", payload);
    else
        cJSON_AddObjectToObject(root, "payload");

    return root;
}

static char *envelope_to_str(cJSON *root)
{
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}

/* ── Factory functions ── */

char *msg_make_telemetry(const char *hub_id, const char *friendly_name,
                         const char *ieee, const char *z2m_json)
{
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "ieee", ieee ? ieee : "");
    cJSON_AddStringToObject(p, "friendly_name", friendly_name);

    cJSON *data = cJSON_Parse(z2m_json);
    if (data)
        cJSON_AddItemToObject(p, "data", data);
    else
        cJSON_AddStringToObject(p, "raw", z2m_json);

    return envelope_to_str(make_envelope("telemetry", hub_id, p));
}

char *msg_make_ack(const char *hub_id, const char *ref_id, const char *result)
{
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "ref_id", ref_id);
    cJSON_AddStringToObject(p, "result", result);
    return envelope_to_str(make_envelope("ack", hub_id, p));
}

char *msg_make_pair_result(const char *hub_id, bool success,
                           const char *ieee, const char *model,
                           const char *friendly_name)
{
    cJSON *p = cJSON_CreateObject();
    cJSON_AddBoolToObject(p, "success", success);
    if (ieee && ieee[0])            cJSON_AddStringToObject(p, "ieee", ieee);
    if (model && model[0])          cJSON_AddStringToObject(p, "model", model);
    if (friendly_name && friendly_name[0]) cJSON_AddStringToObject(p, "friendly_name", friendly_name);
    return envelope_to_str(make_envelope("pair_result", hub_id, p));
}

char *msg_make_status(const char *hub_id, uint64_t uptime,
                      int devices_count, const char *z2m_status)
{
    cJSON *p = cJSON_CreateObject();
    cJSON_AddNumberToObject(p, "uptime", (double)uptime);
    cJSON_AddNumberToObject(p, "devices_count", devices_count);
    cJSON_AddStringToObject(p, "z2m_status", z2m_status);
    return envelope_to_str(make_envelope("status", hub_id, p));
}

char *msg_make_ping(const char *hub_id)
{
    return envelope_to_str(make_envelope("ping", hub_id, NULL));
}
