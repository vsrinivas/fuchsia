// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/profile_provider.h"

#include <lib/syslog/cpp/logger.h>

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
      [callback = std::move(callback),
       thread_handle = std::move(thread_handle)](zx::profile profile) {
        FX_DCHECK(profile);
        if (profile) {
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

}  // namespace media::audio
