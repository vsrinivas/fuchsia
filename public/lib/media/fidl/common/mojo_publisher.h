// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_MEDIA_COMMON_MOJO_PUBLISHER_H_
#define SERVICES_MEDIA_COMMON_MOJO_PUBLISHER_H_

#include <functional>
#include <vector>

namespace mojo {
namespace media {

// Implements pull mode publishing (e.g. MediaPlayer::GetStatus).
template <typename TCallback>
class MojoPublisher {
 public:
  using CallbackRunner = std::function<void(const TCallback&, uint64_t)>;

  // Sets the callback runner. This method must be called before calling Get
  // or Updated. The callback runner calls a single callback using current
  // information.
  void SetCallbackRunner(const CallbackRunner& callback_runner) {
    MOJO_DCHECK(callback_runner);
    callback_runner_ = callback_runner;
  }

  // Handles a get request from the client. This method should be called from
  // the mojo 'get' method (e.g. MediaPlayer::GetStatus).
  void Get(uint64_t version_last_seen, const TCallback& callback) {
    MOJO_DCHECK(callback_runner_);

    if (version_last_seen < version_) {
      callback_runner_(callback, version_);
    } else {
      pending_callbacks_.push_back(callback);
    }
  }

  // Increments the version number and runs pending callbacks. This method
  // should be called whenever the published information should be sent to
  // subscribing clients.
  void SendUpdates() {
    MOJO_DCHECK(callback_runner_);

    ++version_;

    std::vector<TCallback> pending_callbacks;
    pending_callbacks_.swap(pending_callbacks);

    for (const TCallback& pending_callback : pending_callbacks) {
      callback_runner_(pending_callback, version_);
    }
  }

 private:
  uint64_t version_ = 1u;
  std::vector<TCallback> pending_callbacks_;
  CallbackRunner callback_runner_;
};

}  // namespace media
}  // namespace mojo

#endif  // SERVICES_MEDIA_COMMON_MOJO_PUBLISHER_H_
