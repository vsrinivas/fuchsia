// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_STREAM_SINK_RELEASE_FENCE_H_
#define SRC_MEDIA_VNEXT_LIB_STREAM_SINK_RELEASE_FENCE_H_

namespace fmlib {

// An empty abstract class. Objects of this type are deleted to signal that payload resources
// associated with a packet are available for reuse. This scheme is an abstraction of the
// |zx::eventpair| release fence used in the FIDL stream transport. We use the abstraction rather
// than |zx::eventpair| so that the same scheme can be used in other contexts.
class ReleaseFence {
 public:
  virtual ~ReleaseFence() = default;
};

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_STREAM_SINK_RELEASE_FENCE_H_
