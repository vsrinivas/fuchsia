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
                                 time_server::RoughTimeServer rough_time_server,
                                 async_dispatcher_t* dispatcher, RetryConfig retry_config)
    : context_(std::move(context)),
      rough_time_server_(std::move(rough_time_server)),
      push_source_binding_(this),
      status_watcher_(time_external::Status::OK),
      dispatcher_(dispatcher),
      consecutive_poll_failures_(0),
      retry_config_(retry_config) {
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
  // TODO: trigger a check for when network becomes available so we can properly
  // report the INITIALIZING state rather than starting on OK.
}

TimeServiceImpl::~TimeServiceImpl() = default;

void TimeServiceImpl::AsyncPollSamples(async_dispatcher_t* dispatcher, async::TaskBase* task,
                                       zx_status_t zx_status) {
  zx_time_t before = zx_clock_get_monotonic();
  auto ret = rough_time_server_.GetTimeFromServer();
  zx_time_t after = zx_clock_get_monotonic();

  time_external::Status status;
  if (ret.first == time_server::OK && ret.second) {
    time_external::TimeSample sample;
    sample.set_monotonic((before + after) / 2);
    sample.set_utc(ret.second->get());
    sample_watcher_.Update(std::move(sample));
    dispatcher_last_success_time_.emplace(async::Now(dispatcher));
    status = time_external::Status::OK;
    consecutive_poll_failures_ = 0;
  } else {
    switch (ret.first) {
      case time_server::OK:
        status = time_external::Status::UNKNOWN_UNHEALTHY;
        FX_LOGS(ERROR) << "Time server indicated OK status but did not return a time";
        break;
      case time_server::BAD_RESPONSE:
        FX_LOGS(INFO) << "Failed to poll time with BAD_RESPONSE";
        status = time_external::Status::PROTOCOL;
        break;
      case time_server::NETWORK_ERROR:
        FX_LOGS(INFO) << "Failed to poll time with NETWORK_ERROR";
        status = time_external::Status::NETWORK;
        break;
      case time_server::NOT_SUPPORTED:
      default:
        FX_LOGS(INFO) << "Failed to poll time";
        status = time_external::Status::UNKNOWN_UNHEALTHY;
        break;
    }
    zx::time next_poll_time =
        async::Now(dispatcher_) + retry_config_.WaitAfterFailure(consecutive_poll_failures_);
    ScheduleAsyncPoll(next_poll_time);
    consecutive_poll_failures_++;
  }

  status_watcher_.Update(status);
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
  if (!status_watcher_.Watch(std::move(callback))) {
    // failure to watch indicates we have multiple concurrent WatchSample calls so close the
    // channel.
    ResetPushSourceClient(ZX_ERR_BAD_STATE);
    return;
  }
}

void TimeServiceImpl::ResetPushSourceClient(zx_status_t epitaph) {
  push_source_binding_.Close(epitaph);
  push_source_binding_.Unbind();
  sample_watcher_.ResetClient();
  status_watcher_.ResetClient();
}

}  // namespace network_time_service
