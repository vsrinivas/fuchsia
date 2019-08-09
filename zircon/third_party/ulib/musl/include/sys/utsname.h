#ifndef SYSROOT_SYS_UTSNAME_H_
#define SYSROOT_SYS_UTSNAME_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>
#include <limits.h>

struct utsname {
  char sysname[65];
  char nodename[HOST_NAME_MAX + 1];
  char release[65];
  char version[65];
  char machine[65];
#ifdef _GNU_SOURCE
  char domainname[65];
#else
  char __domainname[65];
#endif
};

int uname(struct utsname*);

#ifdef __cplusplus
}
#endif

#endif  // SYSROOT_SYS_UTSNAME_H_
