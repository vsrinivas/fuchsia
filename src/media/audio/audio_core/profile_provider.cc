// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/profile_provider.h"

#include <lib/syslog/cpp/macros.h>

#include "src/media/audio/audio_core/threading_model.h"
#include "src/media/audio/audio_core/utils.h"

namespace media::audio {

fidl::InterfaceRequestHandler<fuchsia::media::ProfileProvider>
ProfileProvider::GetFidlRequestHandler() {
  return bindings_.GetHandler(this);
}

void ProfileProvider::RegisterHandler(zx::thread thread_handle, std::string name, int64_t period,
                                      RegisterHandlerCallback callback) {
  AcquireRelativePriorityProfile(
      /* HIGH_PRIORITY in zircon */ 24, &context_,
      [callback = std::move(callback), thread_handle = std::move(thread_handle)](
          zx_status_t status, zx::profile profile) {
        FX_DCHECK(status == ZX_OK);
        if (status == ZX_OK) {
          zx_status_t status = thread_handle.set_profile(profile, 0);
          if (status != ZX_OK) {
            FX_LOGS(WARNING) << "Failed to set profile";
          }
        } else {
          FX_LOGS(WARNING) << "Failed to acquire profile";
        }
        callback(/* period= */ 0, /* capacity= */ 0);
      });
}

void ProfileProvider::RegisterHandlerWithCapacity(zx::thread thread_handle, std::string name,
                                                  int64_t period, float capacity_weight,
                                                  RegisterHandlerCallback callback) {
  if (!profile_provider_) {
    profile_provider_ = context_.svc()->Connect<fuchsia::scheduler::ProfileProvider>();
  }
  zx::duration interval = period ? zx::duration(period) : ThreadingModel::kMixProfilePeriod;
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

}  // namespace media::audio
