// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/time/network_time_service/watcher.h"

namespace network_time_service {

bool SampleWatcher::Watch(time_external::PushSource::WatchSampleCallback callback) {
  if (!callback_) {
    callback_ = std::move(callback);
    CallbackIfNeeded();
    return true;
  }
  return false;
}

void SampleWatcher::Update(time_external::TimeSample new_sample) {
  current_ = std::move(new_sample);
  CallbackIfNeeded();
}

void SampleWatcher::ResetClient() {
  last_sent_.reset();
  callback_.reset();
}

time_external::TimeSample SampleWatcher::CloneTimeSample(const time_external::TimeSample& sample) {
  time_external::TimeSample clone;
  sample.Clone(&clone);
  return clone;
}

void SampleWatcher::CallbackIfNeeded() {
  if (!callback_) {
    return;
  }
  if (current_ && (!last_sent_ || last_sent_->monotonic() != current_->monotonic())) {
    callback_.value()(CloneTimeSample(current_.value()));
    callback_.reset();
    last_sent_ = CloneTimeSample(current_.value());
  }
}

}  // namespace network_time_service
