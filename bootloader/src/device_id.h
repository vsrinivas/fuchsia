#pragma once

#include <inet6.h>

#define DEVICE_ID_MAX 24

void device_id(mac_addr addr, char out[DEVICE_ID_MAX]);
