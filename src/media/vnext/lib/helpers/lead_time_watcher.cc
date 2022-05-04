// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/vnext/lib/helpers/lead_time_watcher.h"

namespace fmlib {

void LeadTimeWatcher::Watch(int64_t min, int64_t max,
                            fit::function<void(fuchsia::media2::WatchLeadTimeResult)> callback) {
  FX_CHECK(callback);

  RespondToPendingCall();

  min_ = min;
  max_ = max;

  // Respond immediately, if the bounds are exceeded.
  if (OutsideLimits(lead_time_)) {
    callback(fidl::Clone(lead_time_));
    return;
  }

  // Save the new pending callback.
  callback_ = std::move(callback);
}

void LeadTimeWatcher::Report(zx::duration lead_time) {
  lead_time_ = fuchsia::media2::WatchLeadTimeResult::WithValue(lead_time.get());

  if (callback_ && OutsideLimits(lead_time_)) {
    RespondToPendingCall();
  }
}

void LeadTimeWatcher::ReportUnderflow() {
  lead_time_ = fuchsia::media2::WatchLeadTimeResult::WithUnderflow({});

  if (callback_ && OutsideLimits(lead_time_)) {
    RespondToPendingCall();
  }
}

void LeadTimeWatcher::RespondAndReset() {
  RespondToPendingCall();
  lead_time_ = fuchsia::media2::WatchLeadTimeResult::WithNoValue({});
}

void LeadTimeWatcher::RespondToPendingCall() {
  if (callback_) {
    callback_(fidl::Clone(lead_time_));
    callback_ = nullptr;
  }
}

bool LeadTimeWatcher::OutsideLimits(const fuchsia::media2::WatchLeadTimeResult& lead_time) {
  switch (lead_time.Which()) {
    case fuchsia::media2::WatchLeadTimeResult::Tag::kValue:
      return lead_time.value() < min_ || lead_time.value() > max_;
    case fuchsia::media2::WatchLeadTimeResult::Tag::kUnderflow:
      return kUnderflowLeadTimeValue < min_ || kUnderflowLeadTimeValue > max_;
    case fuchsia::media2::WatchLeadTimeResult::Tag::kNoValue:
      return false;
    default:
      FX_CHECK(false);
      return false;
  }
}

}  // namespace fmlib
