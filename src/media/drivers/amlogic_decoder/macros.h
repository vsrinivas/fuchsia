// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_MACROS_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_MACROS_H_

#include <assert.h>
#include <lib/syslog/global.h>
#include <zircon/syscalls.h>

#include <chrono>
#include <thread>

#include <ddk/debug.h>

// Using the global logger helps keep messages in order when debugging interaction between driver
// code and library code used by the driver which is using the global logger (via chromium_utils.h).
//
// At some point we may instead wish to build libs used by this driver with an iostreams-style
// logging wrapper on top of zxlogf(), to allow logs from the driver to all go through zxlogf() and
// stay ordered that way.
#define USE_GLOBAL_LOGGER 1

// severity can be ERROR, WARNING, INFO, DEBUG, TRACE.  This is the intersection
// of ddk/debug.h and lib/syslog/logger.h.  Both versions of LOG() ensure that
// the severity is valid regardless of USE_GLOBAL_LOGGER setting.
//
// Using ## __VA_ARGS__ instead of __VA_OPT__(,) __VA_ARGS__ for now, since
// __VA_OPT__ doesn't seem to be available yet.
#if USE_GLOBAL_LOGGER
#define LOG(severity, fmt, ...)                                                               \
  do {                                                                                        \
    static_assert(true || DDK_LOG_##severity);                                                \
    static_assert(true || FX_LOG_##severity);                                                 \
    FX_LOGF(severity, "amlogic-video", "[%s:%d] " fmt "", __func__, __LINE__, ##__VA_ARGS__); \
  } while (0)
#else
#define LOG(severity, fmt, ...)                                                                 \
  do {                                                                                          \
    static_assert(true || DDK_LOG_##severity);                                                  \
    static_assert(true || FX_LOG_##severity);                                                   \
    zxlogf(severity, "[%s:%s:%d] " fmt "", "amlogic-video", __func__, __LINE__, ##__VA_ARGS__); \
  } while (0)
#endif

#define DECODE_ERROR(fmt, ...) LOG(ERROR, fmt, ##__VA_ARGS__)

#define DECODE_INFO(fmt, ...) LOG(INFO, fmt, ##__VA_ARGS__)

#ifndef AMLOGIC_DLOG_ENABLE
#define AMLOGIC_DLOG_ENABLE 0
#endif

#define DLOG(fmt, ...)               \
  do {                               \
    if (AMLOGIC_DLOG_ENABLE) {       \
      LOG(INFO, fmt, ##__VA_ARGS__); \
    }                                \
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
      std::chrono::duration_cast<std::chrono::high_resolution_clock::duration>(timeout);
  while (!condition()) {
    if (std::chrono::high_resolution_clock::now() - start >= cast_timeout) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return true;
}

template <typename DurationType, typename T>
__WARN_UNUSED_RESULT bool SpinWaitForRegister(DurationType timeout, T condition) {
  auto start = std::chrono::high_resolution_clock::now();
  auto cast_timeout =
      std::chrono::duration_cast<std::chrono::high_resolution_clock::duration>(timeout);
  while (!condition()) {
    if (std::chrono::high_resolution_clock::now() - start >= cast_timeout) {
      return condition();
    }
  }
  return true;
}

inline void DebugWrite(const char* log) { zx_debug_write(log, strlen(log)); }

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_MACROS_H_
