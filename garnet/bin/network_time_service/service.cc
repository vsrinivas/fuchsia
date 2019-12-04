// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/network_time_service/service.h"

#include <zircon/syscalls.h>

#include <fstream>

#include "src/lib/syslog/cpp/logger.h"

namespace network_time_service {

TimeServiceImpl::TimeServiceImpl(std::unique_ptr<sys::ComponentContext> context,
                                 const char server_config_path[], const char rtc_device_path[])
    : context_(std::move(context)), time_server_(server_config_path, rtc_device_path) {
  context_->outgoing()->AddPublicService(deprecated_bindings_.GetHandler(this));
}

TimeServiceImpl::~TimeServiceImpl() = default;

void TimeServiceImpl::Update(uint8_t num_retries, UpdateCallback callback) {
  bool succeeded = time_server_.UpdateSystemTime(num_retries);
  if (!succeeded) {
    FX_LOGS(ERROR) << "Failed to update system time after " << static_cast<int>(num_retries)
                   << " attempts";
  }
  callback(succeeded);
}

}  // namespace network_time_service
