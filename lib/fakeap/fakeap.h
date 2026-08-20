#ifndef _FAKEAP_H_
#define _FAKEAP_H_
#include "esp_event.h"
#include <ap_config.h>

void create_fake_ap(ap_config_t config);

void create_config(ap_config_t *config);

#endif