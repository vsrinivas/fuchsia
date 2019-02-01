// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_MACROS_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_MACROS_H_

#include <ddk/debug.h>

#include <chrono>
#include <thread>

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

// Wait for a condition to become true, with a timeout.
template <typename DurationType, typename T>
bool WaitForRegister(DurationType timeout, T condition) {
  auto start = std::chrono::high_resolution_clock::now();
  auto cast_timeout =
      std::chrono::duration_cast<std::chrono::high_resolution_clock::duration>(
          timeout);
  while (!condition()) {
    if (std::chrono::high_resolution_clock::now() - start >= cast_timeout) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return true;
}

template <typename DurationType, typename T>
__WARN_UNUSED_RESULT bool SpinWaitForRegister(DurationType timeout,
                                              T condition) {
  auto start = std::chrono::high_resolution_clock::now();
  auto cast_timeout =
      std::chrono::duration_cast<std::chrono::high_resolution_clock::duration>(
          timeout);
  while (!condition()) {
    if (std::chrono::high_resolution_clock::now() - start >= cast_timeout) {
      return condition();
    }
  }
  return true;
}

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_MACROS_H_
