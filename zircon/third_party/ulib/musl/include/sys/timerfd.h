#ifndef SYSROOT_SYS_TIMERFD_H_
#define SYSROOT_SYS_TIMERFD_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <fcntl.h>
#include <time.h>

#define TFD_NONBLOCK O_NONBLOCK
#define TFD_CLOEXEC O_CLOEXEC

#define TFD_TIMER_ABSTIME 1

struct itimerspec;

int timerfd_create(int, int);
int timerfd_settime(int, int, const struct itimerspec*, struct itimerspec*);
int timerfd_gettime(int, struct itimerspec*);

#ifdef __cplusplus
}
#endif

#endif  // SYSROOT_SYS_TIMERFD_H_
