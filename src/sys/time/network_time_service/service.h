// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_TIME_NETWORK_TIME_SERVICE_SERVICE_H_
#define SRC_SYS_TIME_NETWORK_TIME_SERVICE_SERVICE_H_

#include <fuchsia/deprecatedtimezone/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#include <vector>

#include "lib/fidl/cpp/binding_set.h"
#include "src/sys/time/lib/network_time/system_time_updater.h"
#include "src/sys/time/lib/network_time/time_server_config.h"

namespace network_time_service {

// Implementation of the FIDL time service. Handles setting/getting the
// timezone offset by ICU timezone ID.  Also supports getting the raw UTC
// offset in minutes.
//
// For information on ICU ID's and timezone information see:
// http://userguide.icu-project.org/formatparse/datetime
class TimeServiceImpl : public fuchsia::deprecatedtimezone::TimeService {
  // The type of the callback is identical between the two namespaces.
  using fuchsia::deprecatedtimezone::TimeService::UpdateCallback;

 public:
  // Constructs the time service with a caller-owned application context.
  TimeServiceImpl(std::unique_ptr<sys::ComponentContext> context,
                  time_server::SystemTimeUpdater time_updater,
                  time_server::RoughTimeServer rough_time_server);
  ~TimeServiceImpl();

  // |TimeServiceImpl|:
  void Update(uint8_t num_retries, UpdateCallback callback) override;

 private:
  // Attempt to retrieve UTC and update system time without retries.
  std::optional<zx::time_utc> UpdateSystemTime();

  std::unique_ptr<sys::ComponentContext> context_;
  fidl::BindingSet<fuchsia::deprecatedtimezone::TimeService> deprecated_bindings_;
  time_server::SystemTimeUpdater time_updater_;
  time_server::RoughTimeServer rough_time_server_;
};

}  // namespace network_time_service

#endif  // SRC_SYS_TIME_NETWORK_TIME_SERVICE_SERVICE_H_
