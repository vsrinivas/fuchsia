// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_EFFECTS_CONTROLLER_IMPL_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_EFFECTS_CONTROLLER_IMPL_H_

#include <fuchsia/media/audio/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include "src/media/audio/audio_core/v1/context.h"

namespace media::audio {

class EffectsControllerImpl : public fuchsia::media::audio::EffectsController {
 public:
  EffectsControllerImpl(Context& context);

  fidl::InterfaceRequestHandler<fuchsia::media::audio::EffectsController> GetFidlRequestHandler();

 private:
  void UpdateEffect(std::string effect_name, std::string message, UpdateEffectCallback callback);

  Context& context_;
  fidl::BindingSet<fuchsia::media::audio::EffectsController> bindings_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_EFFECTS_CONTROLLER_IMPL_H_
