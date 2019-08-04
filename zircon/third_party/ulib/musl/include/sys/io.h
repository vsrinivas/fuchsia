#ifndef SYSROOT_SYS_IO_H_
#define SYSROOT_SYS_IO_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#include <bits/io.h>

int iopl(int);
int ioperm(unsigned long, unsigned long, int);

#ifdef __cplusplus
}
#endif

#endif  // SYSROOT_SYS_IO_H_
