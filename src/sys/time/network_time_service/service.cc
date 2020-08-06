// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/time/network_time_service/service.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/syscalls.h>

#include <fstream>

namespace network_time_service {

TimeServiceImpl::TimeServiceImpl(std::unique_ptr<sys::ComponentContext> context,
                                 const char server_config_path[], const char rtc_device_path[])
    : context_(std::move(context)), time_server_(server_config_path, rtc_device_path) {
  context_->outgoing()->AddPublicService(deprecated_bindings_.GetHandler(this));
}

TimeServiceImpl::~TimeServiceImpl() = default;

void TimeServiceImpl::Update(uint8_t num_retries, UpdateCallback callback) {
  std::optional<zx::time_utc> result = time_server_.UpdateSystemTime(num_retries);
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

}  // namespace network_time_service
