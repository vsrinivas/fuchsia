// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings/internal/bounds_checker.h"

#include <limits>

#include "lib/fidl/cpp/bindings/internal/bindings_serialization.h"
#include "lib/fxl/logging.h"

namespace fidl {
namespace internal {

BoundsChecker::BoundsChecker(const void* data,
                             uint32_t data_num_bytes,
                             size_t num_handles)
    : data_begin_(reinterpret_cast<uintptr_t>(data)),
      data_end_(data_begin_ + data_num_bytes),
      handle_begin_(0),
      handle_end_(static_cast<uint32_t>(num_handles)) {
  if (data_end_ < data_begin_) {
    // The calculation of |data_end_| overflowed.
    // It shouldn't happen but if it does, set the range to empty so
    // IsValidRange() and ClaimMemory() always fail.
    FXL_DCHECK(false) << "Not reached";
    data_end_ = data_begin_;
  }
  if (handle_end_ < num_handles ||
      num_handles > std::numeric_limits<int32_t>::max()) {
    // Assigning |num_handles| to |handle_end_| overflowed.
    // It shouldn't happen but if it does, set the handle index range to empty.
    FXL_DCHECK(false) << "Not reached";
    handle_end_ = 0;
  }
}

BoundsChecker::~BoundsChecker() {}

bool BoundsChecker::ClaimMemory(const void* position, uint32_t num_bytes) {
  uintptr_t begin = reinterpret_cast<uintptr_t>(position);
  uintptr_t end = begin + num_bytes;

  if (!InternalIsValidRange(begin, end))
    return false;

  data_begin_ = end;
  return true;
}

bool BoundsChecker::ClaimHandle(WrappedHandle encoded_handle) {
  zx_handle_t index = encoded_handle.value;
  if (index == kEncodedInvalidHandleValue)
    return true;

  if (index < handle_begin_ ||
      index >= handle_end_)
    return false;

  // |index| + 1 shouldn't overflow, because |index| is not the max value of
  // numeric_limts<int32_t>::max (it is less than |handle_end_|).
  handle_begin_ = index + 1;
  return true;
}

bool BoundsChecker::IsValidRange(const void* position,
                                 uint32_t num_bytes) const {
  uintptr_t begin = reinterpret_cast<uintptr_t>(position);
  uintptr_t end = begin + num_bytes;

  return InternalIsValidRange(begin, end);
}

bool BoundsChecker::InternalIsValidRange(uintptr_t begin, uintptr_t end) const {
  return end > begin && begin >= data_begin_ && end <= data_end_;
}

}  // namespace internal
}  // namespace fidl
