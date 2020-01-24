// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scenic/util/scheduler_profile.h"

#include <lib/zx/channel.h>
#include <lib/zx/profile.h>
#include <lib/zx/thread.h>

#include "fuchsia/scheduler/cpp/fidl.h"
#include "lib/fdio/directory.h"
#include "src/lib/fxl/logging.h"

namespace util {

// Returns a handle to a scheduler profile for the specified deadline parameters.
zx::profile GetSchedulerProfile(zx::duration capacity, zx::duration deadline, zx::duration period) {
  // Connect to the scheduler profile service to request a new profile.
  zx::channel channel0, channel1;
  zx_status_t status;

  status = zx::channel::create(0u, &channel0, &channel1);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create channel pair: " << status;
    return {};
  }

  status = fdio_service_connect(
      (std::string("/svc/") + fuchsia::scheduler::ProfileProvider::Name_).c_str(), channel0.get());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to connect to profile provider: " << status;
    return {};
  }

  fuchsia::scheduler::ProfileProvider_SyncProxy provider{std::move(channel1)};

  zx_status_t fidl_status;
  zx::profile profile;
  status = provider.GetDeadlineProfile(capacity.get(), deadline.get(), period.get(), "scenic/main",
                                       &fidl_status, &profile);
  if (status != ZX_OK || fidl_status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to request profile: " << status << ", " << fidl_status;
    return {};
  }

  return profile;
}

}  // namespace util
