// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_TIME_NETWORK_TIME_SERVICE_SERVICE_H_
#define SRC_SYS_TIME_NETWORK_TIME_SERVICE_SERVICE_H_

#include <fuchsia/time/external/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/sys/cpp/component_context.h>

#include <algorithm>
#include <vector>

#include "lib/fidl/cpp/binding_set.h"
#include "src/sys/time/lib/network_time/time_server_config.h"
#include "src/sys/time/network_time_service/watcher.h"

const uint64_t kMinNanosBetweenFailures = 1 * 1'000'000'000u;
const uint32_t kMaxRetryExponent = 3;
const uint32_t kTriesPerExponent = 3;
const uint64_t kNanosBetweenSuccesses = 30 * 60 * ((uint64_t)1'000'000'000u);

namespace time_external = fuchsia::time::external;
namespace network_time_service {

// Defines how the |TimeServiceImpl| PushSource polls for updates. Retry time for
// errors begins with |min_nanos_between_failures|, and doubles after |tries_per_exponent|
// failures.
class RetryConfig {
 public:
  RetryConfig(uint64_t min_nanos_between_failures = kMinNanosBetweenFailures,
              uint32_t max_exponent = kMaxRetryExponent,
              uint32_t tries_per_exponent = kTriesPerExponent,
              uint64_t nanos_between_successes = kNanosBetweenSuccesses)
      : nanos_between_successes(nanos_between_successes),
        min_nanos_between_failures_(min_nanos_between_failures),
        max_exponent_(max_exponent),
        tries_per_exponent_(tries_per_exponent){};

  // Returns the duration to wait for the |retry_number|th retry. The first retry
  // is denoted 0.
  zx::duration WaitAfterFailure(uint32_t retry_number) {
    uint32_t exponent = std::min(retry_number / tries_per_exponent_, max_exponent_);
    return zx::nsec(min_nanos_between_failures_ << exponent);
  }

  uint64_t nanos_between_successes;

 private:
  uint64_t min_nanos_between_failures_;
  uint32_t max_exponent_;
  uint32_t tries_per_exponent_;
};

// Implementation of the FIDL time services.
// TODO(fxbug.dev/58068): This currently assumes that there is only a single client. To support
// multiple clients, this needs to retain per-client state so that it understands when
// a value hasn't been returned yet to a particular client, and so that it can close
// channels to only a single client as needed.
class TimeServiceImpl : public time_external::PushSource {
 public:
  // Constructs the time service with a caller-owned application context.
  TimeServiceImpl(std::unique_ptr<sys::ComponentContext> context,
                  time_server::RoughTimeServer rough_time_server, async_dispatcher_t* dispatcher,
                  RetryConfig retry_config = RetryConfig());
  ~TimeServiceImpl();

  // |PushSource|:
  void UpdateDeviceProperties(time_external::Properties properties) override;

  // |PushSource|:
  void WatchSample(WatchSampleCallback callback) override;

  // |PushSource|:
  void WatchStatus(WatchStatusCallback callback) override;

 private:
  // Polls for new time samples and post changes to the time source status.
  void AsyncPollSamples(async_dispatcher_t* dispatcher, async::TaskBase* task, zx_status_t status);

  // Schedules a sample poll to begin at the specified time in the dispatcher's clock.
  void ScheduleAsyncPoll(zx::time dispatch_time);

  // Remove the PushSource client with the specified epitaph and reset client state.
  void ResetPushSourceClient(zx_status_t epitaph);

  std::unique_ptr<sys::ComponentContext> context_;
  time_server::RoughTimeServer rough_time_server_;

  fidl::Binding<time_external::PushSource> push_source_binding_;
  Watcher<time_external::Status> status_watcher_;
  Watcher<time_external::TimeSample> sample_watcher_;

  async_dispatcher_t* dispatcher_;
  // Time of last successful update. Reported in the dispatcher's clock which may not be monotonic.
  std::optional<zx::time> dispatcher_last_success_time_;
  uint32_t consecutive_poll_failures_;
  async::TaskMethod<TimeServiceImpl, &TimeServiceImpl::AsyncPollSamples> sample_poll_task_{this};
  RetryConfig retry_config_;
};

// Estimate the standard deviation based on the monotonic times taken before and after a server was
// polled.
zx_time_t EstimateStandardDeviation(zx_time_t mono_before, zx_time_t mono_after);

}  // namespace network_time_service

#endif  // SRC_SYS_TIME_NETWORK_TIME_SERVICE_SERVICE_H_
