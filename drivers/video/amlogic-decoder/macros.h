// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MACROS_H_
#define MACROS_H_

#include <ddk/debug.h>

#define DECODE_ERROR(fmt, ...) \
  zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)

#define DECODE_INFO(fmt, ...) \
  zxlogf(INFO, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)

#ifndef AMLOGIC_DLOG_ENABLE
#define AMLOGIC_DLOG_ENABLE 0
#endif

#define DLOG(...)               \
  do {                          \
    if (AMLOGIC_DLOG_ENABLE) {  \
      DECODE_INFO(__VA_ARGS__); \
    }                           \
  } while (0)

inline uint32_t truncate_to_32(uint64_t input) {
  assert(!(input & 0xffffffff00000000ul));
  return static_cast<uint32_t>(input);
}

#endif  // MACROS_H_
