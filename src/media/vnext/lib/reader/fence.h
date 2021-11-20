// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_READER_FENCE_H_
#define SRC_MEDIA_VNEXT_LIB_READER_FENCE_H_

#include <lib/fpromise/bridge.h>

#include <vector>

namespace fmlib {

// Creates promises that complete when this object is completed.
class Fence {
 public:
  // Returns a promise that completes when this |Fence| is completed.
  [[nodiscard]] fpromise::promise<> When() {
    if (completed_) {
      return fpromise::make_ok_promise();
    }

    fpromise::bridge<> bridge;
    completers_.push_back(std::move(bridge.completer));
    return bridge.consumer.promise();
  }

  // Indicates that this |Fence| is completed.
  void Complete() {
    completed_ = true;
    std::vector<fpromise::completer<>> completers;
    completers.swap(completers_);
    for (auto& completer : completers) {
      completer.complete_ok();
    }
  }

 private:
  bool completed_ = false;
  std::vector<fpromise::completer<>> completers_;
};

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_READER_FENCE_H_
