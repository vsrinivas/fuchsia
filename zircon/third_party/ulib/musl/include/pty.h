#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/ioctl.h>
#include <termios.h>

int openpty(int*, int*, char*, const struct termios*, const struct winsize*);

#ifdef __cplusplus
}
#endif
