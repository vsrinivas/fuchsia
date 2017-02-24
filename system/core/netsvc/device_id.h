#pragma once

#define DEVICE_ID_MAX 24

void device_id_get(unsigned char mac[6], char out[DEVICE_ID_MAX]);
