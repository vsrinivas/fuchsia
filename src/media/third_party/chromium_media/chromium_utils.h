// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_THIRD_PARTY_CHROMIUM_MEDIA_CHROMIUM_UTILS_H_
#define SRC_MEDIA_THIRD_PARTY_CHROMIUM_MEDIA_CHROMIUM_UTILS_H_

#include <algorithm>
#include <deque>
#include <memory>
#include <optional>

#include <fbl/algorithm.h>
#include <lib/fit/function.h>
#include <lib/stdcompat/span.h>
#include <lib/syslog/cpp/macros.h>
#include <safemath/safe_math.h>
#include <zircon/compiler.h>
#include "safemath/safe_conversions.h"
#include "time_delta.h"

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/lib/fxl/strings/string_printf.h"

#define MEDIA_EXPORT
#define MEDIA_GPU_EXPORT

#define DCHECK FX_DCHECK
#define DCHECK_GE(a, b) FX_DCHECK((a) >= (b))
#define DCHECK_GT(a, b) FX_DCHECK((a) > (b))
#define DCHECK_LT(a, b) FX_DCHECK((a) < (b))
#define DCHECK_LE(a, b) FX_DCHECK((a) <= (b))
#define DCHECK_EQ(a, b) FX_DCHECK((a) == (b))
#define DCHECK_NE(a, b) FX_DCHECK((a) != (b))

#define CHECK FX_CHECK
#define CHECK_LT(a, b) FX_CHECK((a) < (b))
#define CHECK_LE(a, b) FX_CHECK((a) <= (b))
#define CHECK_GT(a, b) FX_CHECK((a) > (b))
#define CHECK_GE(a, b) FX_CHECK((a) >= (b))
#define CHECK_EQ(a, b) FX_CHECK((a) == (b))

#ifndef DLOG
#define DLOG FX_DLOGS
#endif

#ifndef VLOG
#define VLOG FX_VLOGS
#endif

#define FORCE_ALL_LOGS 0
#if !FORCE_ALL_LOGS
#define DVLOG FX_DVLOGS
#define DVLOG_IF(verbose_level, condition)               \
  FX_LAZY_STREAM(FX_VLOG_STREAM(verbose_level, nullptr), \
                 FX_VLOG_IS_ON(verbose_level) && (condition))
#define DVLOGF(verbosity) FX_DVLOGS(verbosity)
#else
// These force logging to be enabled:
#define DVLOG(verbosity) \
  FX_LAZY_STREAM(FX_LOG_STREAM(ERROR, ""), (verbosity) <= 4)
#define DVLOG_IF(verbose_level, condition) \
  FX_LAZY_STREAM(FX_LOG_STREAM(ERROR, ""), (condition))
#define DVLOGF(verbosity) FX_LOGS(ERROR)
#endif

#define VLOGF(verbosity) FX_VLOGS(verbosity)

#define NOTREACHED FX_NOTREACHED
#define NOTIMPLEMENTED FX_NOTIMPLEMENTED

#define WARN_UNUSED_RESULT __WARN_UNUSED_RESULT
#define FALLTHROUGH __FALLTHROUGH

#define SEQUENCE_CHECKER(name) static_assert(true, "")
#define DCHECK_CALLED_ON_VALID_SEQUENCE(name, ...)
#define DETACH_FROM_SEQUENCE(name)

#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&) = delete;      \
  TypeName& operator=(const TypeName&) = delete

// The main difference between scoped_refptr and shared_ptr is that
// scoped_refptr is intrusive, so you can make a new refptr from a raw pointer.
// That isn't used much in this codebase, so ignore it.
template <typename T>
using scoped_refptr = std::shared_ptr<T>;

// Fuchsia supports C++17, so use std::optional for base::Optional.
namespace base {
template <typename T>
using Optional = std::optional<T>;
inline constexpr std::nullopt_t nullopt = std::nullopt;

template <typename T, size_t N>
constexpr size_t size(const T (&array)[N]) noexcept {
  return N;
}

template <typename T, typename... Args>
inline auto MakeRefCounted = std::make_shared<T, Args...>;

// base/span.h
template <typename T>
using span = cpp20::span<T>;

// base/numerics/checked_math.h
template <typename T>
using CheckedNumeric = safemath::internal::CheckedNumeric<T>;
using safemath::checked_cast;
using safemath::IsValueInRangeForNumericType;
using safemath::saturated_cast;
using safemath::strict_cast;

// base/callback_forward.h
using OnceClosure = fit::callback<void()>;
using RepeatingClosure = fit::function<void()>;
template <typename T>
using OnceCallback = fit::callback<T>;

// base/containers/circular_deque.h
template <typename T>
using circular_deque = std::deque<T>;

// base/memory/weak_ptr.h
template <typename T>
using WeakPtr = std::weak_ptr<T>;

template <typename T>
using WeakPtrFactory = fxl::WeakPtrFactory<T>;

// base/cxx17_backports.h
using std::clamp;

// base/sys_byteorder.h
inline uint16_t NetToHost16(uint16_t x) {
  return __builtin_bswap16(x);
}
inline uint32_t NetToHost32(uint32_t x) {
  return __builtin_bswap32(x);
}
inline uint64_t NetToHost64(uint64_t x) {
  return __builtin_bswap64(x);
}

// Converts the bytes in |x| from host to network order (endianness), and
// returns the result.
inline uint16_t HostToNet16(uint16_t x) {
  return __builtin_bswap16(x);
}
inline uint32_t HostToNet32(uint32_t x) {
  return __builtin_bswap32(x);
}
inline uint64_t HostToNet64(uint64_t x) {
  return __builtin_bswap64(x);
}

// base/strings/stringprintf.h
inline auto StringPrintf = fxl::StringPrintf;

// base/bits.h
namespace bits {
template <class T,
          class U,
          class L = std::conditional_t<sizeof(T) >= sizeof(U), T, U>,
          class = std::enable_if_t<std::is_unsigned_v<T>>,
          class = std::enable_if_t<std::is_unsigned_v<U>>>
constexpr const L AlignUp(const T& val_, const U& multiple_) {
  const L val = static_cast<L>(val_);
  const L multiple = static_cast<L>(multiple_);
  return val == 0 ? 0
         : cpp20::has_single_bit<L>(multiple)
             ? (val + (multiple - 1)) & ~(multiple - 1)
             : ((val + (multiple - 1)) / multiple) * multiple;
}
template <typename T, unsigned bits = sizeof(T) * 8>
constexpr typename std::enable_if<std::is_unsigned<T>::value && sizeof(T) <= 8,
                                  unsigned>::type
CountLeadingZeroBits(T value) {
  static_assert(bits > 0, "invalid instantiation");
  return value ? bits == 64
                     ? __builtin_clzll(static_cast<uint64_t>(value))
                     : __builtin_clz(static_cast<uint32_t>(value)) - (32 - bits)
               : bits;
}

constexpr int Log2Ceiling(uint32_t n) {
  // When n == 0, we want the function to return -1.
  // When n == 0, (n - 1) will underflow to 0xFFFFFFFF, which is
  // why the statement below starts with (n ? 32 : -1).
  return (n ? 32 : -1) - CountLeadingZeroBits(n - 1);
}
}  // namespace bits
}  // namespace base

namespace media {

namespace limits {
enum {
  // Clients take care of their own frame requirements
  kMaxVideoFrames = 0,
};

}  // namespace limits

}  // namespace media

#endif  // SRC_MEDIA_THIRD_PARTY_CHROMIUM_MEDIA_CHROMIUM_UTILS_H_
