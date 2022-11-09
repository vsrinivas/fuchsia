// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_PROFILE_PROVIDER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_PROFILE_PROVIDER_H_

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/time.h>

#include "src/media/audio/audio_core/shared/mix_profile_config.h"

namespace media::audio {

class ProfileProvider : public fuchsia::media::ProfileProvider {
 public:
  ProfileProvider(sys::ComponentContext& context, const MixProfileConfig& mix_profile_config)
      : context_(context), mix_profile_period_(mix_profile_config.period) {}

  fidl::InterfaceRequestHandler<fuchsia::media::ProfileProvider> GetFidlRequestHandler();

  // fuchsia::media::ProfileProvider implementation.
  void RegisterHandlerWithCapacity(zx::thread thread_handle, std::string name, int64_t period,
                                   float utilization,
                                   RegisterHandlerWithCapacityCallback callback) override;
  void UnregisterHandler(zx::thread thread_handle, std::string name,
                         UnregisterHandlerCallback callback) override;

 private:
  fidl::BindingSet<fuchsia::media::ProfileProvider, ProfileProvider*> bindings_;
  sys::ComponentContext& context_;
  zx::duration mix_profile_period_;
  fuchsia::scheduler::ProfileProviderPtr profile_provider_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_PROFILE_PROVIDER_H_
