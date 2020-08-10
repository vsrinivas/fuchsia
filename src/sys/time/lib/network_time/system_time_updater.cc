// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/time/lib/network_time/system_time_updater.h"

#include <fcntl.h>
#include <inttypes.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/syslog/cpp/macros.h>
#include <sys/time.h>
#include <unistd.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <string>

#include "fuchsia/hardware/rtc/cpp/fidl.h"
#include "src/sys/time/lib/network_time/time_util.h"
#include "zircon/system/ulib/zx/include/lib/zx/channel.h"

namespace time_server {

namespace rtc = fuchsia::hardware::rtc;

bool SystemTimeUpdater::SetSystemTime(zx::time_utc time) {
  int64_t epoch_seconds = time.get() / 1'000'000'000;
  struct tm ptm;
  gmtime_r(&epoch_seconds, &ptm);
  const rtc::Time rtc_time = ToRtcTime(&ptm);

  rtc::DeviceSyncPtr rtc_device_ptr;
  zx_status_t status = fdio_service_connect(rtc_service_path_.c_str(),
                                            rtc_device_ptr.NewRequest().TakeChannel().release());

  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Couldn't open RTC service at " << rtc_service_path_ << ": "
                   << strerror(errno);
    return false;
  }

  zx_status_t set_status;
  status = rtc_device_ptr->Set(rtc_time, &set_status);
  if ((status != ZX_OK) || (set_status != ZX_OK)) {
    FX_LOGS(ERROR) << "rtc::DeviceSyncPtr->Set failed: " << status << "/" << set_status << " for "
                   << ToIso8601String(epoch_seconds) << " (" << epoch_seconds << ")";
    return false;
  }
  FX_LOGS(INFO) << "time set to: " << ToIso8601String(epoch_seconds);
  return true;
}

}  // namespace time_server
