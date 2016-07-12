#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <bits/ioctl.h>

int ioctl(int, int, ...);

#ifdef __cplusplus
}
#endif
