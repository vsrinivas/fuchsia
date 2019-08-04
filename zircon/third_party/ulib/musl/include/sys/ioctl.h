#ifndef SYSROOT_SYS_IOCTL_H_
#define SYSROOT_SYS_IOCTL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <bits/ioctl.h>

int ioctl(int, int, ...);

#ifdef __cplusplus
}
#endif

#endif  // SYSROOT_SYS_IOCTL_H_
