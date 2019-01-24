// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/network_time/timezone.h"

#include <fcntl.h>
#include <fuchsia/hardware/rtc/c/fidl.h>
#include <inttypes.h>
#include <sys/time.h>
#include <unistd.h>

#include <string>

#include "garnet/bin/network_time/roughtime_server.h"
#include "garnet/bin/network_time/time_server_config.h"
#include "garnet/bin/network_time/time_util.h"
#include "lib/fdio/util.h"
#include "lib/syslog/cpp/logger.h"

namespace time_server {

bool Timezone::Run() {
  FX_LOGS(INFO) << "started";
  return UpdateSystemTime(255);
}

bool Timezone::UpdateSystemTime(int tries) {
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

  for (int i = 0; i < tries; i++) {
    FX_VLOGS(1) << "Updating system time, attempt: " << i + 1;
    roughtime::rough_time_t timestamp_us;
    Status ret = server->GetTimeFromServer(&timestamp_us);
    if (ret == NETWORK_ERROR) {
      if (i != tries - 1) {
        FX_VLOGS(1) << "Can't get time, sleeping for 1 sec";
        sleep(1);
      } else {
        FX_VLOGS(1) << "Can't get time after " << tries << " attempts, abort";
        return false;
      }
      continue;
    } else if (ret != OK) {
      FX_LOGS(ERROR) << "Error with roughtime server, abort";
      return false;
    }
    if (SetSystemTime(timestamp_us / 1'000'000)) {
      break;
    }
  }
  return true;
}

bool Timezone::SetSystemTime(time_t epoch_seconds) {
  struct tm ptm;
  gmtime_r(&epoch_seconds, &ptm);
  fuchsia_hardware_rtc_Time rtc;
  rtc.seconds = ptm.tm_sec;
  rtc.minutes = ptm.tm_min;
  rtc.hours = ptm.tm_hour;
  rtc.day = ptm.tm_mday;
  rtc.month = ptm.tm_mon + 1;
  rtc.year = ptm.tm_year + 1900;
  int rtc_fd = open("/dev/class/rtc/000", O_WRONLY);
  if (rtc_fd < 0) {
    FX_LOGS(ERROR) << "Couldn't open RTC file: " << strerror(errno);
    return false;
  }
  zx_handle_t handle;
  zx_status_t status = fdio_get_service_handle(rtc_fd, &handle);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Couldn't get service handle: " << status;
    return false;
  }

  zx_status_t set_status;
  status = fuchsia_hardware_rtc_DeviceSet(handle, &rtc, &set_status);
  if ((status != ZX_OK) || (set_status != ZX_OK)) {
    FX_LOGS(ERROR) << "fuchsia_hardware_rtc_DeviceSet failed: " << status << "/"
                   << set_status << " for " << ToIso8601String(epoch_seconds)
                   << " (" << epoch_seconds << ")";
    return false;
  }
  FX_LOGS(INFO) << "time set to: " << ToIso8601String(epoch_seconds);
  return true;
}

}  // namespace time_server
