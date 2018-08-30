// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/network_time/timezone.h"

#include <fcntl.h>
#include <inttypes.h>
#include <sys/time.h>
#include <unistd.h>
#include <zircon/device/rtc.h>

#include <string>

#include "garnet/bin/network_time/roughtime_server.h"
#include "garnet/bin/network_time/time_server_config.h"
#include "garnet/bin/network_time/time_util.h"
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
    roughtime::rough_time_t timestamp;
    Status ret = server->GetTimeFromServer(&timestamp);
    if (ret == NETWORK_ERROR) {
      if (i != tries - 1) {
        FX_VLOGS(1) << "Can't get time, sleeping for 1 sec";
        sleep(1);
      }
      continue;
    } else if (ret != OK) {
      FX_LOGS(ERROR) << "Error with roughtime server, abort";
      return false;
    }
    if (SetSystemTime(timestamp / 1'000'000)) {
      break;
    }
  }
  return true;
}

bool Timezone::SetSystemTime(time_t epoch_seconds) {
  struct tm ptm;
  gmtime_r(&epoch_seconds, &ptm);
  rtc_t rtc;
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
  ssize_t written = ioctl_rtc_set(rtc_fd, &rtc);
  if (written != sizeof(rtc)) {
    FX_LOGS(ERROR) << "ioctl_rtc_set failed: " << strerror(errno) << " for "
                   << ToIso8601String(epoch_seconds) << " (" << epoch_seconds
                   << ")";
    return false;
  }
  FX_LOGS(INFO) << "time set to: " << ToIso8601String(epoch_seconds);
  return true;
}

}  // namespace time_server
