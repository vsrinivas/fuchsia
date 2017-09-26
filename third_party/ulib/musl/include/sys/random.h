#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int getentropy(void* buffer, size_t length) __attribute__((__warn_unused_result__));

#ifdef __cplusplus
}
#endif
