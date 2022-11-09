// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/shared/profile_acquirer.h"

#include <fuchsia/scheduler/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/zx/channel.h>

#include <cstdlib>

#include "src/media/audio/audio_core/shared/mix_profile_config.h"

namespace media::audio {

zx_status_t AcquireHighPriorityProfile(const MixProfileConfig& mix_profile_config,
                                       zx::profile* profile) {
  TRACE_DURATION("audio", "AcquireHighPriorityProfile");
  // Use threadsafe static initialization to get our one-and-only copy of this profile object. Each
  // subsequent call will return a duplicate of that profile handle to ensure sharing of thread
  // pools.
  static zx::profile high_priority_profile;
  static zx_status_t initial_status = [&mix_profile_config](zx::profile* profile) {
    zx::channel ch0, ch1;
    zx_status_t res = zx::channel::create(0u, &ch0, &ch1);
    if (res != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to create channel, res=" << res;
      return res;
    }

    res = fdio_service_connect(
        (std::string("/svc/") + fuchsia::scheduler::ProfileProvider::Name_).c_str(), ch0.release());
    if (res != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to connect to ProfileProvider, res=" << res;
      return res;
    }

    fuchsia::scheduler::ProfileProvider_SyncProxy provider(std::move(ch1));

    zx_status_t fidl_status;
    zx::profile res_profile;
    res = provider.GetDeadlineProfile(
        mix_profile_config.capacity.get(), mix_profile_config.deadline.get(),
        mix_profile_config.period.get(), "src/media/audio/audio_core", &fidl_status, &res_profile);
    if (res != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to create profile, res=" << res;
      return res;
    }
    if (fidl_status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to create profile, fidl_status=" << fidl_status;
      return fidl_status;
    }

    *profile = std::move(res_profile);
    return ZX_OK;
  }(&high_priority_profile);

  // If the initial acquisition of the profile failed, return that status.
  if (initial_status != ZX_OK)
    return initial_status;

  // Otherwise, dupe this handle and return it.
  return high_priority_profile.duplicate(ZX_RIGHT_SAME_RIGHTS, profile);
}

void AcquireRelativePriorityProfile(uint32_t priority, sys::ComponentContext* context,
                                    fit::function<void(zx_status_t, zx::profile)> callback) {
  auto nonce = TRACE_NONCE();
  TRACE_DURATION("audio", "AcquireRelativePriorityProfile");
  TRACE_FLOW_BEGIN("audio", "GetProfile", nonce);
  auto profile_provider = context->svc()->Connect<fuchsia::scheduler::ProfileProvider>();
  profile_provider->GetProfile(
      priority, "src/media/audio/audio_core/audio_core_impl",
      // Note we move the FIDL ptr into the closure to ensure we keep the channel open until we
      // receive the callback, otherwise it will be impossible to get a response.
      [profile_provider = std::move(profile_provider), callback = std::move(callback), nonce](
          zx_status_t status, zx::profile profile) {
        TRACE_DURATION("audio", "GetProfile callback");
        TRACE_FLOW_END("audio", "GetProfile", nonce);
        if (status == ZX_OK) {
          callback(status, std::move(profile));
        } else {
          callback(status, zx::profile());
        }
      });
}

void AcquireAudioCoreImplProfile(sys::ComponentContext* context,
                                 fit::function<void(zx_status_t, zx::profile)> callback) {
  AcquireRelativePriorityProfile(/* HIGH_PRIORITY in zircon */ 24, context, std::move(callback));
}

}  // namespace media::audio
