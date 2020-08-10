// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/time/network_time_service/service.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/syscalls.h>

#include <fstream>

namespace network_time_service {

TimeServiceImpl::TimeServiceImpl(std::unique_ptr<sys::ComponentContext> context,
                                 time_server::SystemTimeUpdater time_updater,
                                 time_server::RoughTimeServer rough_time_server)
    : context_(std::move(context)),
      time_updater_(std::move(time_updater)),
      rough_time_server_(std::move(rough_time_server)) {
  context_->outgoing()->AddPublicService(deprecated_bindings_.GetHandler(this));
}

TimeServiceImpl::~TimeServiceImpl() = default;

void TimeServiceImpl::Update(uint8_t num_retries, UpdateCallback callback) {
  FX_LOGS(INFO) << "Updating system time";
  std::optional<zx::time_utc> result = std::nullopt;
  for (uint8_t i = 0; i < num_retries; i++) {
    result = UpdateSystemTime();
    if (result) {
      break;
    }
    zx_nanosleep(zx_deadline_after(ZX_MSEC(500)));
  }
  if (!result) {
    FX_LOGS(ERROR) << "Failed to update system time after " << static_cast<int>(num_retries)
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
  if (!time_updater_.SetSystemTime(*ret.second)) {
    FX_LOGS(ERROR) << "Inexplicably failed to set system time";
    return std::nullopt;
  }
  return ret.second;
}

}  // namespace network_time_service
