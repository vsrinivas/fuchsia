// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/effects_loader/effects_loader.h"

#include <dlfcn.h>

#include "src/lib/fxl/logging.h"

namespace media::audio {

zx_status_t EffectsLoader::LoadLibrary() {
  if (module_) {
    return ZX_ERR_ALREADY_EXISTS;
  }
  module_ = EffectsModuleV1::Open(lib_name_);
  if (!module_) {
    return ZX_ERR_UNAVAILABLE;
  }
  return ZX_OK;
}

zx_status_t EffectsLoader::GetNumFx(uint32_t* num_fx_out) {
  if (!module_) {
    return ZX_ERR_NOT_FOUND;
  }
  if (num_fx_out == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  *num_fx_out = module_->num_effects;
  return ZX_OK;
}

zx_status_t EffectsLoader::GetFxInfo(uint32_t effect_id, fuchsia_audio_effects_description* desc) {
  if (!module_) {
    return ZX_ERR_NOT_FOUND;
  }
  if (desc == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (effect_id >= module_->num_effects) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (!module_->get_info || !module_->get_info(effect_id, desc)) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

Effect EffectsLoader::CreateEffect(uint32_t effect_id, uint32_t frame_rate, uint16_t channels_in,
                                   uint16_t channels_out, std::string_view config) {
  if (!module_) {
    return {};
  }
  if (effect_id >= module_->num_effects) {
    return {};
  }
  if (!module_->create_effect) {
    return {};
  }

  auto handle = module_->create_effect(effect_id, frame_rate, channels_in, channels_out,
                                       config.data(), config.size());
  if (handle == FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE) {
    return {};
  }
  return {handle, module_};
}

}  // namespace media::audio
