// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_UTILS_UTC_TIME_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_UTILS_UTC_TIME_PROVIDER_H_

#include <fuchsia/time/cpp/fidl.h>
#include <lib/sys/cpp/service_directory.h>

#include <optional>
#include <string>

#include "src/lib/timekeeper/system_clock.h"

namespace forensics {

// Provides the UTC time only if the device's system clock is accurate.
class UtcTimeProvider {
 public:
  // fuchsia.time.Utc is expected to be in |services|.
  UtcTimeProvider(std::shared_ptr<sys::ServiceDirectory> services, timekeeper::Clock* clock);

  // Returns the current UTC time if the device's UTC time is accurate, std::nullopt otherwise.
  std::optional<zx::time_utc> CurrentTime() const;

 private:
  // Keeps making asynchronous calls until the UTC time is accurate.
  void WatchForAccurateUtcTime();

  std::shared_ptr<sys::ServiceDirectory> services_;
  timekeeper::Clock* clock_;

  fuchsia::time::UtcPtr utc_;

  bool is_utc_time_accurate_ = false;
};

}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_UTILS_UTC_TIME_PROVIDER_H_
