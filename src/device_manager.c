//
//  device_manager.c
//  smart_home
//
//  Created by  Alexey on 13.03.2026.
//

#include "device_manager.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void devmgr_init(device_manager_t *dm)
{
    memset(dm, 0, sizeof(*dm));
}

device_t *devmgr_find_by_ieee(device_manager_t *dm, const char *ieee)
{
    for (int i = 0; i < dm->count; i++)
        if (strcmp(dm->devices[i].ieee, ieee) == 0)
            return &dm->devices[i];
    return NULL;
}

device_t *devmgr_find_by_name(device_manager_t *dm, const char *name)
{
    for (int i = 0; i < dm->count; i++)
        if (strcmp(dm->devices[i].friendly_name, name) == 0)
            return &dm->devices[i];
    return NULL;
}

int devmgr_count(const device_manager_t *dm)
{
    return dm->count;
}

int devmgr_update_device_list(device_manager_t *dm, const char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root || !cJSON_IsArray(root)) {
        if (root) cJSON_Delete(root);
        return -1;
    }

    int new_count = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, root) {
        if (new_count >= DM_MAX_DEVICES) break;

        cJSON *type = cJSON_GetObjectItem(item, "type");
        if (cJSON_IsString(type) && strcmp(type->valuestring, "Coordinator") == 0)
            continue;

        device_t *dev = &dm->devices[new_count];

        /* Preserve existing state if ieee matches */
        cJSON *ieee_j = cJSON_GetObjectItem(item, "ieee_address");
        const char *ieee_str = cJSON_IsString(ieee_j) ? ieee_j->valuestring : "";
        device_t *existing = devmgr_find_by_ieee(dm, ieee_str);

        if (existing && existing != dev) {
            /* Copy state from existing slot */
            memcpy(dev->states, existing->states, sizeof(dev->states));
            dev->state_count = existing->state_count;
            dev->last_seen = existing->last_seen;
            dev->is_online = existing->is_online;
        } else if (!existing) {
            dev->state_count = 0;
            dev->last_seen = time(NULL);
            dev->is_online = true;
        }

        strncpy(dev->ieee, ieee_str, DM_MAX_IEEE - 1);

        cJSON *v;
        if ((v = cJSON_GetObjectItem(item, "friendly_name")) && cJSON_IsString(v))
            strncpy(dev->friendly_name, v->valuestring, DM_MAX_NAME - 1);

        cJSON *def = cJSON_GetObjectItem(item, "definition");
        if (def) {
            if ((v = cJSON_GetObjectItem(def, "model")) && cJSON_IsString(v))
                strncpy(dev->model, v->valuestring, DM_MAX_MODEL - 1);
            if ((v = cJSON_GetObjectItem(def, "vendor")) && cJSON_IsString(v))
                strncpy(dev->manufacturer, v->valuestring, DM_MAX_MANUF - 1);
        }

        if (cJSON_IsString(type))
            strncpy(dev->device_type, type->valuestring, sizeof(dev->device_type) - 1);

        new_count++;
    }

    dm->count = new_count;
    cJSON_Delete(root);
    LOG_INFO("devmgr", "Device list updated: %d devices", dm->count);
    return 0;
}

device_t *devmgr_update_state(device_manager_t *dm, const char *friendly_name,
                               const char *json_str)
{
    device_t *dev = devmgr_find_by_name(dm, friendly_name);
    if (!dev) {
        LOG_DEBUG("devmgr", "State for unknown: %s", friendly_name);
        return NULL;
    }

    cJSON *root = cJSON_Parse(json_str);
    if (!root) return NULL;

    dev->last_seen = time(NULL);
    dev->is_online = true;

    cJSON *child;
    cJSON_ArrayForEach(child, root) {
        const char *key = child->string;
        if (!key) continue;

        /* Skip internal Z2M fields */
        if (strcmp(key, "linkquality") == 0 ||
            strcmp(key, "last_seen") == 0 ||
            strcmp(key, "elapsed") == 0) continue;

        /* Find or create state entry */
        dev_state_t *entry = NULL;
        for (int i = 0; i < dev->state_count; i++) {
            if (strcmp(dev->states[i].key, key) == 0) {
                entry = &dev->states[i];
                break;
            }
        }
        if (!entry && dev->state_count < DM_MAX_STATES) {
            entry = &dev->states[dev->state_count++];
            strncpy(entry->key, key, DM_MAX_KEY - 1);
        }
        if (!entry) continue;

        entry->updated_at = time(NULL);

        if (cJSON_IsNumber(child))
            snprintf(entry->value, DM_MAX_VAL, "%g", child->valuedouble);
        else if (cJSON_IsString(child))
            strncpy(entry->value, child->valuestring, DM_MAX_VAL - 1);
        else if (cJSON_IsBool(child))
            strncpy(entry->value, cJSON_IsTrue(child) ? "true" : "false", DM_MAX_VAL - 1);
        else {
            char *p = cJSON_PrintUnformatted(child);
            if (p) { strncpy(entry->value, p, DM_MAX_VAL - 1); free(p); }
        }
    }

    cJSON_Delete(root);
    return dev;
}

const char *devmgr_process_event(device_manager_t *dm, const char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return NULL;

    static char event[64];
    event[0] = '\0';

    cJSON *type = cJSON_GetObjectItem(root, "type");
    if (cJSON_IsString(type))
        strncpy(event, type->valuestring, sizeof(event) - 1);

    cJSON *data = cJSON_GetObjectItem(root, "data");

    if (strcmp(event, "device_joined") == 0 || strcmp(event, "device_announce") == 0) {
        const char *ieee  = "";
        const char *fname = "";
        cJSON *v;
        if (data) {
            if ((v = cJSON_GetObjectItem(data, "ieee_address")) && cJSON_IsString(v))
                ieee = v->valuestring;
            if ((v = cJSON_GetObjectItem(data, "friendly_name")) && cJSON_IsString(v))
                fname = v->valuestring;
        }

        LOG_INFO("devmgr", "Device joined: %s (%s)", fname, ieee);

        if (ieee[0] && !devmgr_find_by_ieee(dm, ieee) && dm->count < DM_MAX_DEVICES) {
            device_t *dev = &dm->devices[dm->count++];
            memset(dev, 0, sizeof(*dev));
            strncpy(dev->ieee, ieee, DM_MAX_IEEE - 1);
            strncpy(dev->friendly_name, fname, DM_MAX_NAME - 1);
            dev->is_online = true;
            dev->last_seen = time(NULL);
        }
    }

    if (strcmp(event, "device_leave") == 0 && data) {
        cJSON *v = cJSON_GetObjectItem(data, "ieee_address");
        if (cJSON_IsString(v)) {
            LOG_INFO("devmgr", "Device left: %s", v->valuestring);
            device_t *dev = devmgr_find_by_ieee(dm, v->valuestring);
            if (dev) dev->is_online = false;
        }
    }

    cJSON_Delete(root);
    return event[0] ? event : NULL;
}

void devmgr_set_pair_mode(device_manager_t *dm, bool active, int duration)
{
    dm->pair_mode = active;
    dm->pair_started = active ? time(NULL) : 0;
    dm->pair_duration = duration;
    LOG_INFO("devmgr", "Pair mode: %s (duration=%d)", active ? "ON" : "OFF", duration);
}

bool devmgr_pair_expired(const device_manager_t *dm)
{
    if (!dm->pair_mode) return false;
    return (time(NULL) - dm->pair_started) >= dm->pair_duration;
}

char *devmgr_serialize(const device_manager_t *dm)
{
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < dm->count; i++) {
        const device_t *d = &dm->devices[i];
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "ieee", d->ieee);
        cJSON_AddStringToObject(obj, "name", d->friendly_name);
        cJSON_AddStringToObject(obj, "model", d->model);
        cJSON_AddStringToObject(obj, "manufacturer", d->manufacturer);
        cJSON_AddStringToObject(obj, "type", d->device_type);
        cJSON_AddBoolToObject(obj, "online", d->is_online);
        cJSON_AddNumberToObject(obj, "last_seen", (double)d->last_seen);

        cJSON *states = cJSON_CreateObject();
        for (int j = 0; j < d->state_count; j++)
            cJSON_AddStringToObject(states, d->states[j].key, d->states[j].value);
        cJSON_AddItemToObject(obj, "state", states);

        cJSON_AddItemToArray(arr, obj);
    }
    char *str = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return str;
}
