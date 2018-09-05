// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/frame_fingerprint.h"

namespace zxdb {

bool FrameFingerprint::operator==(const FrameFingerprint& other) const {
  return frame_address_ == other.frame_address_;
}

// static
bool FrameFingerprint::Newer(const FrameFingerprint& left,
                             const FrameFingerprint& right) {
  // Stacks grow "down" so bigger addresses represent older frames.
  return left.frame_address_ < right.frame_address_;
}

}  // namespace
