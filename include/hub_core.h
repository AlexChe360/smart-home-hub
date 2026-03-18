//
//  hub_core.h
//  smart_home
//
//  Created by  Alexey on 12.03.2026.
//

#ifndef HUB_CORE_H
#define HUB_CORE_H

#include "config.h"
#include "mqtt_client.h"
#include "ws_client.h"
#include "device_manager.h"
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    hub_config_t      config;
    mqtt_client_t     mqtt;
    ws_client_t       ws;
    device_manager_t  devmgr;
    time_t            start_time;
    time_t            last_status_report;
    time_t            last_app_ping;
    volatile bool     running;
} hub_core_t;

int  hub_init(hub_core_t *hub, const char *config_path);
int  hub_run(hub_core_t *hub);
void hub_stop(hub_core_t *hub);
void hub_destroy(hub_core_t *hub);

#ifdef __cplusplus
}
#endif

#endif /* HUB_CORE_H */
