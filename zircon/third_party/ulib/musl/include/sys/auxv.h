#ifndef SYSROOT_SYS_AUXV_H_
#define SYSROOT_SYS_AUXV_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <elf.h>

unsigned long getauxval(unsigned long);

#ifdef __cplusplus
}
#endif

#endif  // SYSROOT_SYS_AUXV_H_
