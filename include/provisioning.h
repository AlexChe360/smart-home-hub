//
//  provisioning.h
//  smart_home
//
//  Created by  Alexey on 18.03.2026.
//

#ifndef PROVISIONING_H
#define PROVISIONING_H
 
#include "config.h"
 
#ifdef __cplusplus
extern "C" {
#endif
 
// Зарегистрировать хаб в облаке.
// Использует cfg->hub_id как serial номер.
// При успехе:
//   - cfg->hub_id    заменяется на UUID из облака
//   - cfg->hub_token заменяется на JWT токен хаба
//   - конфиг сохраняется в config_path
// Возвращает 0 при успехе, -1 при ошибке.
int hub_provision(hub_config_t *cfg, const char *config_path);
 
#ifdef __cplusplus
}
#endif
 
#endif /* PROVISIONING_H */
