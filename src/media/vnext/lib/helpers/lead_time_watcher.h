// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_HELPERS_LEAD_TIME_WATCHER_H_
#define SRC_MEDIA_VNEXT_LIB_HELPERS_LEAD_TIME_WATCHER_H_

#include <fuchsia/media2/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

namespace fmlib {

// Manages state for |WatchBufferLeadTime| and |WatchPacketLeadTime| methods.
class LeadTimeWatcher {
 public:
  // Indicates a watch method has been called with the given parameters.
  void Watch(int64_t min, int64_t max,
             fit::function<void(fuchsia::media2::WatchLeadTimeResult)> callback);

  // Reports the current lead time.
  void Report(zx::duration lead_time);

  // Reports underflow.
  void ReportUnderflow();

  // Responds to a pending call, if there is one, and resets this watcher.
  void RespondAndReset();

 private:
  static constexpr int64_t kUnderflowLeadTimeValue = -1;

  // Calls |callback_| if it's value.
  void RespondToPendingCall();

  // Determines whether |lead_time| is outside the current limits given by |min_| and |max_|.
  bool OutsideLimits(const fuchsia::media2::WatchLeadTimeResult& lead_time);

  int64_t min_;
  int64_t max_;
  fit::function<void(fuchsia::media2::WatchLeadTimeResult)> callback_;
  fuchsia::media2::WatchLeadTimeResult lead_time_ =
      fuchsia::media2::WatchLeadTimeResult::WithNoValue({});
};

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_HELPERS_LEAD_TIME_WATCHER_H_
