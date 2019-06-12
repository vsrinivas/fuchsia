// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_NETWORK_TIME_TIMEZONE_H_
#define GARNET_BIN_NETWORK_TIME_TIMEZONE_H_

#include <lib/zx/time.h>
#include <sys/time.h>

#include <string>
#include <utility>

namespace time_server {

const char kRealRtcDevicePath[] = "/dev/class/rtc/000";

// The default number of time update attempts at startup.
const uint32_t kDefaultUpdateAttempts = UINT32_MAX;

// TODO(CP-131): Rename to something like SystemTimeUpdater.
class Timezone {
 public:
  bool Run();
  bool UpdateSystemTime(uint32_t tries);
  static bool SetSystemTime(const std::string& rtc_service_path,
                            zx::time_utc time);
  Timezone(std::string server_config_file,
           std::string rtc_service_path = kRealRtcDevicePath)
      : server_config_file_(std::move(server_config_file)),
        rtc_service_path_(std::move(rtc_service_path)) {}
  ~Timezone() = default;

 private:
  std::string server_config_file_;
  // Path to the FIDL service representing the realtime clock device.
  std::string rtc_service_path_;
};

}  // namespace time_server

#endif  // GARNET_BIN_NETWORK_TIME_TIMEZONE_H_
