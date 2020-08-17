// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_KTL_INCLUDE_TIME_H_
#define ZIRCON_KERNEL_LIB_KTL_INCLUDE_TIME_H_

#include <stddef.h>

// <zircon/time.h> uses these when __has_include(<time.h>).
typedef long int time_t;
struct timespec {
  time_t tv_sec;
  long int tv_nsec;
};

// The kernel doesn't want this file but some libc++ headers we need
// wind up including it and they need these declarations.

#ifdef __cplusplus

using clock_t = long int;
struct tm {};
char* asctime(tm*) noexcept;
clock_t clock() noexcept;
char* ctime(const time_t*) noexcept;
double difftime(time_t, time_t) noexcept;
tm* gmtime(const time_t*) noexcept;
tm* localtime(const time_t*) noexcept;
time_t mktime(tm*) noexcept;
size_t strftime(char*, size_t, const char*, const tm*) noexcept;
time_t time(time_t*) noexcept;
int timespec_get(timespec* ts, int) noexcept;

#endif  // __cplusplus

#endif  // ZIRCON_KERNEL_LIB_KTL_INCLUDE_TIME_H_
