#ifndef SYSROOT_MALLOC_H_
#define SYSROOT_MALLOC_H_

#include <features.h>

#ifdef __cplusplus
extern "C" {
#endif

#define __NEED_size_t
#include <bits/alltypes.h>

void* malloc(size_t) __nothrow_fn;
void* calloc(size_t, size_t) __nothrow_fn;
void* realloc(void*, size_t) __nothrow_fn;
void free(void*) __nothrow_fn;
void* valloc(size_t) __nothrow_fn;
void* memalign(size_t, size_t) __nothrow_fn;

size_t malloc_usable_size(void*) __nothrow_fn;

#ifdef __cplusplus
}
#endif

#endif  // SYSROOT_MALLOC_H_
