// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_H264_CHROMIUM_UTILS_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_H264_CHROMIUM_UTILS_H_

#include <memory>
#include <optional>

#include <fbl/span.h>
#include <safemath/checked_math.h>
#include <zircon/compiler.h>

#include "src/lib/fxl/logging.h"
#include "time_delta.h"

#define MEDIA_EXPORT
#define MEDIA_GPU_EXPORT

#define DCHECK FXL_DCHECK
#define DCHECK_GE(a, b) FXL_DCHECK((a) >= (b))
#define DCHECK_GT(a, b) FXL_DCHECK((a) > (b))
#define DCHECK_LT(a, b) FXL_DCHECK((a) < (b))
#define DCHECK_LE(a, b) FXL_DCHECK((a) <= (b))
#define DCHECK_EQ(a, b) FXL_DCHECK((a) == (b))
#define DCHECK_NE(a, b) FXL_DCHECK((a) != (b))

#define CHECK FXL_CHECK
#ifndef DLOG
#define DLOG FXL_DLOG
#endif
#define DVLOG FXL_DVLOG
#define DVLOG_IF(verbose_level, condition)        \
  FXL_LAZY_STREAM(FXL_VLOG_STREAM(verbose_level), \
                  FXL_VLOG_IS_ON(verbose_level) && (condition))
#define NOTREACHED FXL_NOTREACHED

#define WARN_UNUSED_RESULT __WARN_UNUSED_RESULT
#define FALLTHROUGH __FALLTHROUGH

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

// base/span.h
template <typename T>
using span = fbl::Span<T>;

// base/numerics/checked_math.h
template <typename T>
using CheckedNumeric = safemath::internal::CheckedNumeric<T>;
using safemath::checked_cast;
using safemath::IsValueInRangeForNumericType;
}  // namespace base

namespace media {

namespace limits {
enum {
  // Clients take care of their own frame requirements
  kMaxVideoFrames = 0,
};

}  // namespace limits

}  // namespace media

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_H264_CHROMIUM_UTILS_H_
