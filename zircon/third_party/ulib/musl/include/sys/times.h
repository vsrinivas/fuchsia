#ifndef SYSROOT_SYS_TIMES_H_
#define SYSROOT_SYS_TIMES_H_

#ifdef __cplusplus
extern "C" {
#endif

#define __NEED_clock_t
#include <bits/alltypes.h>

struct tms {
  clock_t tms_utime;
  clock_t tms_stime;
  clock_t tms_cutime;
  clock_t tms_cstime;
};

clock_t times(struct tms*);

#ifdef __cplusplus
}
#endif

#endif  // SYSROOT_SYS_TIMES_H_
