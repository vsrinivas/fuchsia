// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_STUBS_UTC_CLOCK_READY_WATCHER_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_STUBS_UTC_CLOCK_READY_WATCHER_H_

#include "src/developer/forensics/utils/utc_clock_ready_watcher.h"

namespace forensics::stubs {

class UtcClockReadyWatcher : public UtcClockReadyWatcherBase {
 public:
  // Register a callback that will be executed when the utc clock becomes ready.
  void OnClockReady(::fit::callback<void()> callback) override;
  bool IsUtcClockReady() const override;

  void StartClock();

 private:
  std::vector<::fit::callback<void()>> callbacks_;
  bool is_utc_clock_ready_ = false;
};

}  // namespace forensics::stubs

#endif
