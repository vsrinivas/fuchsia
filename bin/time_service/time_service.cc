// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/time_service/time_service.h"

#include <algorithm>
#include <fstream>
#include <memory>

#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_number_conversions.h"

static constexpr char kTzOffsetFile[] = "/data/tz_offset_minutes";
static constexpr int64_t kMinutesInDay = 1440;

namespace time_service {

TimeServiceImpl::TimeServiceImpl() = default;

TimeServiceImpl::~TimeServiceImpl() = default;

void TimeServiceImpl::GetTimezoneOffsetMinutes(
    const GetTimezoneOffsetMinutesCallback &callback) {
  std::ifstream in_fstream(kTzOffsetFile);
  if (!in_fstream.is_open()) {
    // Being unable to open the file for reading most likely means the file
    // doesn't exist. If the user doesn't set a timezone somewhere, then
    // logging this would create unnecessary noise. The important error is when
    // the service is unable to /write/ to the storage file.
    callback(0);
    return;
  }
  std::string offset_str;
  in_fstream >> offset_str;
  in_fstream.close();

  if (offset_str.empty()) {
    FXL_LOG(ERROR) << "TZ offset file empty at '" << kTzOffsetFile << "'";
    callback(0);
    return;
  }
  int64_t offset = fxl::StringToNumber<int64_t>(offset_str);
  callback(offset);
}

void TimeServiceImpl::NotifyWatchers(int64_t offset_change) {
  for (auto& watcher : watchers_) {
    watcher->OnTimezoneOffsetChange(offset_change);
  }
}

void TimeServiceImpl::SetTimezoneOffsetMinutes(
    int64_t offset, const SetTimezoneOffsetMinutesCallback &callback) {
  if (offset >= kMinutesInDay || offset <= -1 * kMinutesInDay) {
    FXL_LOG(ERROR) << "Offset out of 24-hour range: " << offset;
    callback(Status::ERR_INVALID_OFFSET);
    return;
  }
  std::ofstream out_fstream(kTzOffsetFile, std::ofstream::trunc);
  if (!out_fstream.is_open()) {
    FXL_LOG(ERROR) << "Unable to open file for write '" << kTzOffsetFile << "'";
    callback(Status::ERR_WRITE);
    return;
  }
  out_fstream << offset;
  out_fstream.close();
  NotifyWatchers(offset);
  callback(Status::OK);
}

void TimeServiceImpl::ReleaseWatcher(TimeServiceWatcher *watcher) {
  auto predicate = [watcher](const auto &target) {
    return target.get() == watcher;
  };
  watchers_.erase(
      std::remove_if(watchers_.begin(), watchers_.end(), predicate));
}

void TimeServiceImpl::Watch(fidl::InterfaceHandle<TimeServiceWatcher> watcher) {
  TimeServiceWatcherPtr watcher_proxy =
      TimeServiceWatcherPtr::Create(std::move(watcher));
  TimeServiceWatcher *proxy_raw_ptr = watcher_proxy.get();
  watcher_proxy.set_connection_error_handler(
      [this, proxy_raw_ptr] { ReleaseWatcher(proxy_raw_ptr); });
  watchers_.push_back(std::move(watcher_proxy));
}

void TimeServiceImpl::AddBinding(fidl::InterfaceRequest<TimeService> request) {
  bindings_.AddBinding(this, std::move(request));
}

}  // namespace time_service
