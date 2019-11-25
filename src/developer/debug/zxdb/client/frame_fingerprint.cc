// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/frame_fingerprint.h"

#include <inttypes.h>

#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

bool FrameFingerprint::operator==(const FrameFingerprint& other) const {
  return frame_address_ == other.frame_address_ && inline_count_ == other.inline_count_;
}

std::string FrameFingerprint::ToString() const {
  return fxl::StringPrintf("{0x%" PRIx64 ", %zu}", frame_address_, inline_count_);
}

// static
bool FrameFingerprint::Newer(const FrameFingerprint& left, const FrameFingerprint& right) {
  if (left.frame_address_ == right.frame_address_) {
    // Inline functions (in the same physical frame) are newer if the inline stack depth is higher.
    return left.inline_count_ > right.inline_count_;
  }

  // Stacks grow "down" so bigger addresses represent older frames.
  return left.frame_address_ < right.frame_address_;
}

// static
bool FrameFingerprint::NewerOrEqual(const FrameFingerprint& left, const FrameFingerprint& right) {
  return Newer(left, right) || left == right;
}

}  // namespace zxdb
