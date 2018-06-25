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
#include "lib/syslog/cpp/logger.h"

namespace time_zone {

bool Timezone::Run() {
  FX_LOGS(INFO) << "started";
  UpdateSystemTime(3);
  return true;
}

bool Timezone::UpdateSystemTime(uint8_t tries) {
  TimeServerConfig config;
  if (!config.Parse(server_config_file_)) {
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

  for (uint8_t i = 0; i < tries; i++) {
    FX_VLOGS(1) << "Updating system time, attempt: " << i + 1;
    roughtime::rough_time_t timestamp;
    Status ret = server->GetTimeFromServer(&timestamp);
    if (ret == NETWORK_ERROR) {
      if (i != tries - 1) {
        FX_VLOGS(1) << "Can't get time, sleeping for 10 sec";
        sleep(10);
      }
      continue;
    } else if (ret != OK) {
      FX_LOGS(ERROR) << "Error with roughtime server, abort";
      return false;
    }

    struct tm ptm;
    time_t t = timestamp / 1000000;
    gmtime_r(&t, &ptm);
    rtc_t rtc;
    rtc.seconds = ptm.tm_sec;
    rtc.minutes = ptm.tm_min;
    rtc.hours = ptm.tm_hour;
    rtc.day = ptm.tm_mday;
    rtc.month = ptm.tm_mon + 1;
    rtc.year = ptm.tm_year + 1900;
    int rtc_fd = open("/dev/class/rtc/000", O_WRONLY);
    if (rtc_fd < 0) {
      FX_LOGS(ERROR) << "open rtc: " << strerror(errno);
      return false;
    }
    ssize_t written = ioctl_rtc_set(rtc_fd, &rtc);
    if (written != sizeof(rtc)) {
      printf("%04d-%02d-%02dT%02d:%02d:%02d\n", rtc.year, rtc.month, rtc.day,
             rtc.hours, rtc.minutes, rtc.seconds);
      FX_LOGS(ERROR) << "ioctl_rtc_set: " << strerror(errno) << " " << t;
      return false;
    }
    char time[20];
    snprintf(time, 20, "%04d-%02d-%02dT%02d:%02d:%02d", rtc.year, rtc.month,
             rtc.day, rtc.hours, rtc.minutes, rtc.seconds);
    FX_LOGS(INFO) << "time set to: " << time;
    break;
  }
  return true;
}

}  // namespace time_zone
