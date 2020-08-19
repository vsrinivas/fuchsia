// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/time/network_time_service/service.h"

#include <lib/async/cpp/time.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/syscalls.h>

#include <fstream>

namespace network_time_service {

TimeServiceImpl::TimeServiceImpl(std::unique_ptr<sys::ComponentContext> context,
                                 time_server::SystemTimeUpdater time_updater,
                                 time_server::RoughTimeServer rough_time_server,
                                 async_dispatcher_t* dispatcher, RetryConfig retry_config)
    : context_(std::move(context)),
      time_updater_(std::move(time_updater)),
      rough_time_server_(std::move(rough_time_server)),
      push_source_binding_(this),
      dispatcher_(dispatcher),
      retry_config_(retry_config) {
  context_->outgoing()->AddPublicService(deprecated_bindings_.GetHandler(this));

  push_source_binding_.set_error_handler([&](zx_status_t error) {
    // Clean up client state for the next client.
    ResetPushSourceClient(error);
  });
  fidl::InterfaceRequestHandler<time_external::PushSource> handler =
      [&](fidl::InterfaceRequest<time_external::PushSource> request) {
        if (push_source_binding_.is_bound()) {
          FX_LOGS(WARNING) << "Received multiple connection requests which is unsupported";
          request.Close(ZX_ERR_ALREADY_BOUND);
        } else {
          push_source_binding_.Bind(std::move(request));
        }
      };
  context_->outgoing()->AddPublicService(std::move(handler));
}

TimeServiceImpl::~TimeServiceImpl() = default;

void TimeServiceImpl::Update(uint8_t num_retries, UpdateCallback callback) {
  std::optional<zx::time_utc> result = std::nullopt;
  for (uint8_t i = 0; i < num_retries; i++) {
    result = UpdateSystemTime();
    if (result) {
      break;
    }
    zx_nanosleep(zx_deadline_after(ZX_MSEC(500)));
  }
  if (!result) {
    FX_LOGS(WARNING) << "Failed to update system time after " << static_cast<int>(num_retries)
                     << " attempts";
  }
  std::unique_ptr<fuchsia::deprecatedtimezone::UpdatedTime> update = nullptr;
  if (result) {
    update = fuchsia::deprecatedtimezone::UpdatedTime::New();
    update->utc_time = result->get();
  }
  callback(std::move(update));
}

std::optional<zx::time_utc> TimeServiceImpl::UpdateSystemTime() {
  auto ret = rough_time_server_.GetTimeFromServer();
  if (ret.first != time_server::OK) {
    return std::nullopt;
  }
  // TODO(57747): move RTC interactions to Timekeeper before using new API.
  if (!time_updater_.SetSystemTime(*ret.second)) {
    FX_LOGS(ERROR) << "Inexplicably failed to set system time";
    return std::nullopt;
  }
  return ret.second;
}

void TimeServiceImpl::AsyncPollSamples(async_dispatcher_t* dispatcher, async::TaskBase* task,
                                       zx_status_t zx_status) {
  zx_time_t before = zx_clock_get_monotonic();
  auto ret = rough_time_server_.GetTimeFromServer();
  zx_time_t after = zx_clock_get_monotonic();

  if (ret.first == time_server::OK && ret.second) {
    time_external::TimeSample sample;
    sample.set_monotonic((before + after) / 2);
    sample.set_utc(ret.second->get());
    sample_watcher_.Update(std::move(sample));
    dispatcher_last_success_time_.emplace(async::Now(dispatcher));
    return;
  }
  if (ret.first == time_server::OK) {
    FX_LOGS(ERROR) << "Time server indicated OK status but did not return a time";
  }
  zx::time next_poll_time =
      async::Now(dispatcher_) + zx::nsec(retry_config_.nanos_between_failures);
  ScheduleAsyncPoll(next_poll_time);
}

void TimeServiceImpl::ScheduleAsyncPoll(zx::time dispatch_time) {
  // try to post a task, ZX_ERR_ALREADY_EXISTS indicates it is already scheduled.
  zx_status_t post_status = sample_poll_task_.PostForTime(dispatcher_, dispatch_time);
  if (post_status != ZX_OK && post_status != ZX_ERR_ALREADY_EXISTS) {
    FX_LOGS(ERROR) << "Failed to post task!";
  }
}

void TimeServiceImpl::UpdateDeviceProperties(time_external::Properties properties) {
  // Time samples are currently taken independently of each other and therefore we don't
  // need to take properties such as oscillator performance into account.
}

void TimeServiceImpl::WatchSample(TimeServiceImpl::WatchSampleCallback callback) {
  if (!sample_watcher_.Watch(std::move(callback))) {
    // failure to watch indicates we have multiple concurrent WatchSample calls so close the
    // channel.
    ResetPushSourceClient(ZX_ERR_BAD_STATE);
    return;
  }

  zx::time next_poll_time(0);
  if (dispatcher_last_success_time_) {
    next_poll_time =
        *dispatcher_last_success_time_ + zx::nsec(retry_config_.nanos_between_successes);
  }
  ScheduleAsyncPoll(next_poll_time);
}

void TimeServiceImpl::WatchStatus(TimeServiceImpl::WatchStatusCallback callback) {
  // TODO(satsukiu) - unimplemented.
  ResetPushSourceClient(ZX_ERR_NOT_SUPPORTED);
}

void TimeServiceImpl::ResetPushSourceClient(zx_status_t epitaph) {
  push_source_binding_.Close(epitaph);
  push_source_binding_.Unbind();
  sample_watcher_.ResetClient();
}

}  // namespace network_time_service
