// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_MACROS_H_
#define SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_MACROS_H_

#include <assert.h>
#include <limits.h>  // PAGE_SIZE
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include <limits>

// Files #including macros.h may assume that it #includes inttypes.h.
// So, for convenience, they don't need to follow "#include-what-you-use" for that header.
#include <inttypes.h>

#include "platform_logger.h"

#ifndef MAGMA_DEBUG_INTERNAL_USE_ONLY
#error MAGMA_DEBUG_INTERNAL_USE_ONLY not defined, your gn foo needs magma_util_config
#endif

namespace magma {

static constexpr bool kDebug = MAGMA_DEBUG_INTERNAL_USE_ONLY;

#define DASSERT(x)                                                                                \
  do {                                                                                            \
    if (magma::kDebug && !(x)) {                                                                  \
      magma::PlatformLogger::Log(magma::PlatformLogger::LOG_ERROR, "%s:%d DASSERT: %s", __FILE__, \
                                 __LINE__, #x);                                                   \
      abort();                                                                                    \
    }                                                                                             \
  } while (0)

#define DMESSAGE(format, ...)                                                                \
  do {                                                                                       \
    if (magma::kDebug) {                                                                     \
      magma::PlatformLogger::Log(magma::PlatformLogger::LOG_INFO, "%s:%d " format, __FILE__, \
                                 __LINE__, ##__VA_ARGS__);                                   \
    }                                                                                        \
  } while (0)

static constexpr bool kMagmaDretEnable = kDebug;

#define DRET(ret)                                                                              \
  (magma::kMagmaDretEnable && ret != 0                                                         \
   ? magma::PlatformLogger::Log(magma::PlatformLogger::LOG_ERROR, "%s:%d Returning error %ld", \
                                __FILE__, __LINE__, (int64_t)(ret)),                           \
   ret : ret)

#define DRET_MSG(ret, format, ...)                                                        \
  (magma::kMagmaDretEnable && ret != 0                                                    \
   ? magma::PlatformLogger::Log(magma::PlatformLogger::LOG_ERROR,                         \
                                "%s:%d Returning error %ld: " format, __FILE__, __LINE__, \
                                (int64_t)ret, ##__VA_ARGS__),                             \
   ret : ret)

#define DRETF(ret, format, ...)                                                       \
  (magma::kMagmaDretEnable && !(ret)                                                  \
   ? magma::PlatformLogger::Log(magma::PlatformLogger::LOG_ERROR,                     \
                                "%s:%d Returning false: " format, __FILE__, __LINE__, \
                                ##__VA_ARGS__),                                       \
   ret : ret)

#define DRETP(ret, format, ...)                                                                    \
  (magma::kMagmaDretEnable && (ret == nullptr)                                                     \
   ? magma::PlatformLogger::Log(magma::PlatformLogger::LOG_ERROR, "%s:%d Returning null: " format, \
                                __FILE__, __LINE__, ##__VA_ARGS__),                                \
   ret : ret)

enum LogLevel { LOG_WARNING, LOG_INFO };

// TODO(fxbug.dev/13095) - replace with MAGMA_LOG
__attribute__((format(printf, 2, 3))) static inline void log(LogLevel level, const char* msg, ...) {
  switch (level) {
    case LOG_WARNING:
      printf("[WARNING] ");
      break;
    case LOG_INFO:
      printf("[INFO] ");
      break;
  }
  va_list args;
  va_start(args, msg);
  vprintf(msg, args);
  va_end(args);
  printf("\n");
}

#define UNIMPLEMENTED(...)                \
  do {                                    \
    DLOG("UNIMPLEMENTED: " #__VA_ARGS__); \
    DASSERT(false);                       \
  } while (0)

#define ATTRIBUTE_UNUSED __attribute__((unused))

#ifndef DISALLOW_COPY_AND_ASSIGN
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&) = delete;      \
  void operator=(const TypeName&) = delete
#endif

static inline uint32_t to_uint32(uint64_t val) {
  DASSERT(val <= std::numeric_limits<uint32_t>::max());
  return static_cast<uint32_t>(val);
}

static inline uint32_t page_size() {
#ifdef PAGE_SIZE
  return PAGE_SIZE;
#else
  long page_size = sysconf(_SC_PAGESIZE);
  DASSERT(page_size > 0);
  return to_uint32(page_size);
#endif
}

static inline uint32_t page_shift() {
#if PAGE_SIZE == 4096
  return 12;
#else
  return __builtin_ctz(::magma::page_size());
#endif
}

static inline bool is_page_aligned(uint64_t val) { return (val & (::magma::page_size() - 1)) == 0; }

static inline uint32_t upper_32_bits(uint64_t n) { return static_cast<uint32_t>(n >> 32); }

static inline uint32_t lower_32_bits(uint64_t n) { return static_cast<uint32_t>(n); }

static inline bool get_pow2(uint64_t val, uint64_t* pow2_out) {
  if (val == 0)
    return DRETF(false, "zero is not a power of two");

  uint64_t result = 0;
  while ((val & 1) == 0) {
    val >>= 1;
    result++;
  }
  if (val >> 1)
    return DRETF(false, "not a power of 2");

  *pow2_out = result;
  return true;
}

static inline bool is_pow2(uint64_t val) {
  uint64_t out;
  return get_pow2(val, &out);
}

// Note, alignment must be a power of 2
template <class T>
static inline T round_up(T val, uint64_t alignment) {
  DASSERT(is_pow2(alignment));
  return ((val - 1) | (alignment - 1)) + 1;
}

static inline uint64_t ns_to_ms(uint64_t ns) { return ns / 1000000ull; }

static inline int64_t ms_to_signed_ns(uint64_t ms) {
  if (ms > INT64_MAX / 1000000)
    return INT64_MAX;
  return static_cast<int64_t>(ms) * 1000000;
}

static inline uint64_t get_monotonic_ns() {
  timespec time;
  if (clock_gettime(CLOCK_MONOTONIC, &time) != 0) {
    DASSERT(false);
    return 0;
  }
  return static_cast<uint64_t>(time.tv_sec) * 1000000000ull + time.tv_nsec;
}

#define MAGMA_THREAD_ANNOTATION(x) __attribute__((x))

#define MAGMA_CAPABILITY(x) MAGMA_THREAD_ANNOTATION(__capability__(x))
#define MAGMA_GUARDED(x) MAGMA_THREAD_ANNOTATION(__guarded_by__(x))
#define MAGMA_ACQUIRE(...) MAGMA_THREAD_ANNOTATION(__acquire_capability__(__VA_ARGS__))
#define MAGMA_TRY_ACQUIRE(...) MAGMA_THREAD_ANNOTATION(__try_acquire_capability__(__VA_ARGS__))
#define MAGMA_ACQUIRED_BEFORE(...) MAGMA_THREAD_ANNOTATION(__acquired_before__(__VA_ARGS__))
#define MAGMA_ACQUIRED_AFTER(...) MAGMA_THREAD_ANNOTATION(__acquired_after__(__VA_ARGS__))
#define MAGMA_RELEASE(...) MAGMA_THREAD_ANNOTATION(__release_capability__(__VA_ARGS__))
#define MAGMA_REQUIRES(...) MAGMA_THREAD_ANNOTATION(__requires_capability__(__VA_ARGS__))
#define MAGMA_EXCLUDES(...) MAGMA_THREAD_ANNOTATION(__locks_excluded__(__VA_ARGS__))
#define MAGMA_RETURN_CAPABILITY(x) MAGMA_THREAD_ANNOTATION(__lock_returned__(x))
#define MAGMA_SCOPED_CAPABILITY MAGMA_THREAD_ANNOTATION(__scoped_lockable__)
#define MAGMA_NO_THREAD_SAFETY_ANALYSIS MAGMA_THREAD_ANNOTATION(__no_thread_safety_analysis__)

}  // namespace magma

#endif  // SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_MACROS_H_
