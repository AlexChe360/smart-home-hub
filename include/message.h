//
//  message.h
//  smart_home
//
//  Created by  Alexey on 12.03.2026.
//

#ifndef message_h
#define message_h

#include "cJSON.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

    /* Message types */
    typedef enum {
      MSG_TELEMETRY = 0,
      MSG_COMMAND,
      MSG_ACK,
      MSG_PAIR_START,
      MSG_PAIR_STOP,
      MSG_PAIR_RESULT,
      MSG_STATUS,
      MSG_RENAME,
      MSG_REMOVE,
      MSG_OTA_UPDATE,
      MSG_PING,
      MSG_PONG,
      MSG_ERROR,
      MSG_UNKNOWN
    } msg_type_t;

    typedef struct {
      msg_type_t type;
      char       id[40];        /* UUID v4 */
      char       hub_id[32];
      uint64_t   timestamp;
      cJSON      *payload;      /* owned = call msg_free() */
    } hub_message_t;

    /* Type <-> string conversion */
    const char *msg_type_str(msg_type_t type);
    msg_type_t msg_type_from_str(const char *str);

    /* Generate UUID v4 into buf (>= 37 bytes) */
    void msg_uuid(char *buf, size_t len);

    /* Parse JSON string into message. Returns 0 on success. Call msg_free() after. */
    int msg_parse(const char *json_str, hub_message_t *msg);

    /* Serialize message to JSON string. Caller must free() returned string. */
    char *msg_serialize(const hub_message_t *msg);

    /* Free message payload */
    void msg_free(hub_message_t *msg);

    /*
     * Convenience factory functions.
     * All return malloc'd JSON string = caller must free().
    */
    char *msg_make_telemetry(const char *hub_id, const char *freindly_name,
                             const char *ieee, const char *z2m_json);

    char *msg_make_ack(const char *hub_id, const char *ref_id, const char *result);

    char *msg_make_pair_result(const char *hub_id, bool success,
                          const char *ieee, const char *model,
                          const char *freindly_name);

    char *msg_make_status(const char *hub_id, uint64_t uptime,
                          int device_count, const char *z2m_status);

    char *msg_make_ping(const char *hub_id);

#ifdef __cplusplus
}
#endif

#endif /* message_h */
