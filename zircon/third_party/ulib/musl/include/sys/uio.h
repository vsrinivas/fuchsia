#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#define __NEED_size_t
#define __NEED_ssize_t
#define __NEED_struct_iovec

#if defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
#define __NEED_off_t
#endif

#include <bits/alltypes.h>

#define UIO_MAXIOV 1024

ssize_t readv(int, const struct iovec*, int);
ssize_t writev(int, const struct iovec*, int);

#if defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
ssize_t preadv(int, const struct iovec*, int, off_t);
ssize_t pwritev(int, const struct iovec*, int, off_t);
#endif

#ifdef __cplusplus
}
#endif
