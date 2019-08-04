#ifndef SYSROOT_SYS_FSUID_H_
#define SYSROOT_SYS_FSUID_H_

#ifdef __cplusplus
extern "C" {
#endif

#define __NEED_uid_t
#define __NEED_gid_t

#include <bits/alltypes.h>

int setfsuid(uid_t);
int setfsgid(gid_t);

#ifdef __cplusplus
}
#endif

#endif  // SYSROOT_SYS_FSUID_H_
