// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_TIME_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_TIME_PROVIDER_H_

#include <lib/async/cpp/wait.h>
#include <lib/zx/clock.h>

#include <memory>

#include "src/developer/forensics/feedback/annotations/provider.h"
#include "src/developer/forensics/feedback/annotations/types.h"
#include "src/lib/timekeeper/clock.h"

namespace forensics::feedback {

// Get the uptime of the device and the current UTC time.
class TimeProvider : public DynamicSyncAnnotationProvider {
 public:
  TimeProvider(async_dispatcher_t* dispatcher, zx::unowned_clock clock_handle,
               std::unique_ptr<timekeeper::Clock> clock);
  virtual ~TimeProvider() = default;

  std::set<std::string> GetKeys() const override;

  Annotations Get() override;

 private:
  // Keep waiting on the clock handle until the clock has started.
  void OnClockStart(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                    const zx_packet_signal_t* signal);

  std::unique_ptr<timekeeper::Clock> clock_;
  bool is_utc_time_accurate_ = false;
  async::WaitMethod<TimeProvider, &TimeProvider::OnClockStart> wait_for_clock_start_;
};

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_TIME_PROVIDER_H_
