#pragma once

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
