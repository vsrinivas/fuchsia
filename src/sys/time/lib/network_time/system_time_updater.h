// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_TIME_LIB_NETWORK_TIME_SYSTEM_TIME_UPDATER_H_
#define SRC_SYS_TIME_LIB_NETWORK_TIME_SYSTEM_TIME_UPDATER_H_

#include <lib/zx/time.h>
#include <sys/time.h>

#include <optional>
#include <string>
#include <utility>

#include "src/sys/time/lib/network_time/roughtime_server.h"
#include "src/sys/time/lib/network_time/time_server_config.h"

namespace time_server {

const char kRealRtcDevicePath[] = "/dev/class/rtc/000";

// The default number of time update attempts at startup.
const uint32_t kDefaultUpdateAttempts = UINT32_MAX;

// Updates the system time accessible through an RTC device.
class SystemTimeUpdater {
 public:
  bool SetSystemTime(zx::time_utc time);
  SystemTimeUpdater(std::string rtc_service_path = kRealRtcDevicePath)
      : rtc_service_path_(std::move(rtc_service_path)) {}
  ~SystemTimeUpdater() = default;

 private:
  // Path to the FIDL service representing the realtime clock device.
  std::string rtc_service_path_;
};

}  // namespace time_server

#endif  // SRC_SYS_TIME_LIB_NETWORK_TIME_SYSTEM_TIME_UPDATER_H_
