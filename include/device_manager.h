//
//  device_manager.h
//  smart_home
//
//  Created by  Alexey on 12.03.2026.
//

#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include "cJSON.h"
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DM_MAX_DEVICES    128
#define DM_MAX_IEEE       24
#define DM_MAX_NAME       100
#define DM_MAX_MODEL      50
#define DM_MAX_MANUF      50
#define DM_MAX_STATES     32
#define DM_MAX_KEY        50
#define DM_MAX_VAL        128

typedef struct {
    char   key[DM_MAX_KEY];
    char   value[DM_MAX_VAL];
    time_t updated_at;
} dev_state_t;

typedef struct {
    char    ieee[DM_MAX_IEEE];
    char    friendly_name[DM_MAX_NAME];
    char    model[DM_MAX_MODEL];
    char    manufacturer[DM_MAX_MANUF];
    char    device_type[24];
    bool    is_online;
    time_t  last_seen;
    dev_state_t states[DM_MAX_STATES];
    int     state_count;
} device_t;

typedef struct {
    device_t devices[DM_MAX_DEVICES];
    int      count;
    bool     pair_mode;
    time_t   pair_started;
    int      pair_duration;
} device_manager_t;

void     devmgr_init(device_manager_t *dm);
int      devmgr_update_device_list(device_manager_t *dm, const char *json_str);
device_t *devmgr_update_state(device_manager_t *dm, const char *friendly_name,
                               const char *json_str);
const char *devmgr_process_event(device_manager_t *dm, const char *json_str);
device_t *devmgr_find_by_ieee(device_manager_t *dm, const char *ieee);
device_t *devmgr_find_by_name(device_manager_t *dm, const char *name);
int      devmgr_count(const device_manager_t *dm);
void     devmgr_set_pair_mode(device_manager_t *dm, bool active, int duration);
bool     devmgr_pair_expired(const device_manager_t *dm);
char    *devmgr_serialize(const device_manager_t *dm); /* caller must free() */

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_MANAGER_H */
