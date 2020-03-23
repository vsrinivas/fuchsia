// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_PROFILE_PROVIDER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_PROFILE_PROVIDER_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

namespace media::audio {

class ProfileProvider : public fuchsia::media::ProfileProvider {
 public:
  ProfileProvider(sys::ComponentContext& context) : context_(context) {}

  fidl::InterfaceRequestHandler<fuchsia::media::ProfileProvider> GetFidlRequestHandler();

  // fuchsia::media::ProfileProvider implementation.
  void RegisterHandler(zx::thread thread_handle, std::string name, int64_t period,
                       RegisterHandlerCallback callback) override;

 private:
  fidl::BindingSet<fuchsia::media::ProfileProvider, ProfileProvider*> bindings_;
  sys::ComponentContext& context_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_PROFILE_PROVIDER_H_
