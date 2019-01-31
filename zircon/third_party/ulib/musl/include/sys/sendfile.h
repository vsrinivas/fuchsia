#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>
#include <unistd.h>

ssize_t sendfile(int, int, off_t*, size_t);

#ifdef __cplusplus
}
#endif
