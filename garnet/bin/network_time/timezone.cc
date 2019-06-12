// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/network_time/timezone.h"

#include <fcntl.h>
#include <inttypes.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <sys/time.h>
#include <unistd.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <string>

#include "fuchsia/hardware/rtc/cpp/fidl.h"
#include "garnet/bin/network_time/roughtime_server.h"
#include "garnet/bin/network_time/time_server_config.h"
#include "garnet/bin/network_time/time_util.h"
#include "lib/syslog/cpp/logger.h"
#include "zircon/system/ulib/zx/include/lib/zx/channel.h"

namespace time_server {

namespace rtc = fuchsia::hardware::rtc;

bool Timezone::Run() {
  FX_LOGS(INFO) << "started";
  return UpdateSystemTime(kDefaultUpdateAttempts);
}

bool Timezone::UpdateSystemTime(uint32_t tries) {
  TimeServerConfig config;
  if (!config.Parse(server_config_file_)) {
    FX_LOGS(ERROR) << "Failed to parse config file";
    return false;
  }

  const RoughTimeServer* server = nullptr;
  std::vector<RoughTimeServer> servers = config.ServerList();
  for (const auto& s : servers) {
    if (!s.IsValid()) {
      continue;
    }
    server = &s;
    break;
  }

  if (server == nullptr) {
    FX_LOGS(ERROR) << "No valid server";
    return false;
  }

  for (uint32_t i = 0; i < tries; i++) {
    FX_VLOGS(1) << "Updating system time, attempt: " << i + 1;
    auto ret = server->GetTimeFromServer();
    if (ret.first == NETWORK_ERROR) {
      if (i != tries - 1) {
        FX_VLOGS(1) << "Can't get time, sleeping for 500ms";
        zx_nanosleep(zx_deadline_after(ZX_MSEC(500)));
      } else {
        FX_LOGS(ERROR) << "Can't get time due to network error after " << tries
                       << " attempts, abort";
        return false;
      }
      continue;
    } else if (ret.first != OK || !ret.second) {
      FX_LOGS(ERROR) << "Error with roughtime server [" << ret.first
                     << "], abort";
      return false;
    }
    if (SetSystemTime(rtc_service_path_, *ret.second)) {
      return true;
    }
  }
  FX_LOGS(ERROR) << "Inexplicably failed to get time after " << tries
                 << " attempts, abort";
  return false;
}

bool Timezone::SetSystemTime(const std::string& rtc_service_path,
                             zx::time_utc time) {
  int64_t epoch_seconds = time.get() / 1'000'000'000;
  struct tm ptm;
  gmtime_r(&epoch_seconds, &ptm);
  const rtc::Time rtc_time = ToRtcTime(&ptm);

  rtc::DeviceSyncPtr rtc_device_ptr;
  zx_status_t status =
      fdio_service_connect(rtc_service_path.c_str(),
                           rtc_device_ptr.NewRequest().TakeChannel().release());

  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Couldn't open RTC service at " << rtc_service_path
                   << ": " << strerror(errno);
    return false;
  }

  zx_status_t set_status;
  status = rtc_device_ptr->Set(rtc_time, &set_status);
  if ((status != ZX_OK) || (set_status != ZX_OK)) {
    FX_LOGS(ERROR) << "rtc::DeviceSyncPtr->Set failed: " << status << "/"
                   << set_status << " for " << ToIso8601String(epoch_seconds)
                   << " (" << epoch_seconds << ")";
    return false;
  }
  FX_LOGS(INFO) << "time set to: " << ToIso8601String(epoch_seconds);
  return true;
}

}  // namespace time_server
