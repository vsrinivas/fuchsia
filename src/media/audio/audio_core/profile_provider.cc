// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/profile_provider.h"

#include <lib/syslog/cpp/macros.h>

#include "src/media/audio/audio_core/mix_profile_config.h"
#include "src/media/audio/audio_core/utils.h"

namespace media::audio {

constexpr int kPriorityDefault = 16;

fidl::InterfaceRequestHandler<fuchsia::media::ProfileProvider>
ProfileProvider::GetFidlRequestHandler() {
  return bindings_.GetHandler(this);
}

void ProfileProvider::RegisterHandlerWithCapacity(zx::thread thread_handle, std::string name,
                                                  int64_t period, float capacity_weight,
                                                  RegisterHandlerWithCapacityCallback callback) {
  if (!profile_provider_) {
    profile_provider_ = context_.svc()->Connect<fuchsia::scheduler::ProfileProvider>();
  }
  zx::duration interval = period ? zx::duration(period) : mix_profile_period_;
  zx::duration capacity(interval.to_nsecs() * capacity_weight);
  profile_provider_->GetDeadlineProfile(
      capacity.get(), interval.get(), interval.get(), name,
      [interval, capacity, callback = std::move(callback),
       thread_handle = std::move(thread_handle)](zx_status_t status, zx::profile profile) {
        if (status != ZX_OK) {
          FX_PLOGS(WARNING, status) << "Failed to acquire deadline profile";
          callback(0, 0);
          return;
        }
        status = thread_handle.set_profile(profile, 0);
        if (status != ZX_OK) {
          FX_PLOGS(WARNING, status) << "Failed to set thread profile";
        }
        callback(interval.get(), capacity.get());
      });
}

void ProfileProvider::UnregisterHandler(zx::thread thread_handle, std::string name,
                                        UnregisterHandlerCallback callback) {
  if (!profile_provider_) {
    profile_provider_ = context_.svc()->Connect<fuchsia::scheduler::ProfileProvider>();
  }

  profile_provider_->GetProfile(
      kPriorityDefault, name,
      [thread_handle = std::move(thread_handle), callback = std::move(callback)](
          zx_status_t status, zx::profile profile) {
        if (status != ZX_OK) {
          FX_PLOGS(WARNING, status) << "Failed to acquire default profile";
          callback();
          return;
        }
        status = thread_handle.set_profile(profile, 0);
        if (status != ZX_OK) {
          FX_PLOGS(WARNING, status) << "Failed to set thread profile";
        }
        callback();
      });
}

}  // namespace media::audio
