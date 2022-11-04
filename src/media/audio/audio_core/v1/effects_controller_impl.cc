// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/effects_controller_impl.h"

#include "src/media/audio/audio_core/v1/audio_device_manager.h"

namespace media::audio {

EffectsControllerImpl::EffectsControllerImpl(Context& context) : context_(context) {}

fidl::InterfaceRequestHandler<fuchsia::media::audio::EffectsController>
EffectsControllerImpl::GetFidlRequestHandler() {
  return bindings_.GetHandler(this);
}

void EffectsControllerImpl::UpdateEffect(std::string effect_name, std::string message,
                                         UpdateEffectCallback callback) {
  auto promise = context_.device_manager().UpdateEffect(effect_name, message);

  context_.threading_model().FidlDomain().executor()->schedule_task(
      promise.then([callback = std::move(callback)](
                       fpromise::result<void, fuchsia::media::audio::UpdateEffectError>& result) {
        if (result.is_ok()) {
          callback(fuchsia::media::audio::EffectsController_UpdateEffect_Result::WithResponse(
              fuchsia::media::audio::EffectsController_UpdateEffect_Response()));
        } else {
          callback(fuchsia::media::audio::EffectsController_UpdateEffect_Result::WithErr(
              result.take_error()));
        }
      }));
}

}  // namespace media::audio
