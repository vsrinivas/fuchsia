// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_TESTING_TEST_FENCE_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_TESTING_TEST_FENCE_H_

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/eventpair.h>

#include <optional>

#include "src/media/audio/services/common/logging.h"

namespace media_audio {

class TestFence {
 public:
  TestFence() {
    auto status = zx::eventpair::create(0, &local_, &*peer_);
    FX_CHECK(status == ZX_OK) << "zx::eventpair::create failed with status " << status;
  }

  // Returns an object that can be used wherever a fence is needed, such as in a packet queue.
  // This method can be called at most once.
  zx::eventpair Take() {
    FX_CHECK(peer_.has_value());
    auto out = std::move(*peer_);
    peer_ = std::nullopt;
    return out;
  }

  // Reports if the fence has been reached.
  bool Done() const { return WaitWithDeadline(zx::time(0)); }

  // Waits for the fence to be reached. Returns false if the timeout passes first.
  bool Wait(zx::duration timeout) const { return WaitWithDeadline(zx::deadline_after(timeout)); }

 private:
  bool WaitWithDeadline(zx::time deadline) const {
    auto status = local_.wait_one(ZX_EVENTPAIR_PEER_CLOSED, deadline, nullptr);
    if (status == ZX_OK) {
      return true;
    }
    if (status != ZX_ERR_TIMED_OUT) {
      FX_PLOGS(ERROR, status) << "unexpected wait_one status";
    }
    return false;
  }

  zx::eventpair local_;
  std::optional<zx::eventpair> peer_ = zx::eventpair();
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_TESTING_TEST_FENCE_H_
