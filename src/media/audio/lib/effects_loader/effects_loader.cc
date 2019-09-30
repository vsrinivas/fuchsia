// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/effects_loader/effects_loader.h"

#include <dlfcn.h>
#include <lib/trace/event.h>

#include "src/lib/fxl/logging.h"

namespace media::audio {
namespace {

// With |num_effects| == 0, none of the functions should ever be used (ex: there's no effects to
// query for information, to create, etc).
const fuchsia_audio_effects_module_v1 kNullEffectModuleV1 = {
    .num_effects = 0,
    .get_info = nullptr,
    .create_effect = nullptr,
    .update_effect_configuration = nullptr,
    .delete_effect = nullptr,
    .get_parameters = nullptr,
    .process_inplace = nullptr,
    .process = nullptr,
    .flush = nullptr,
};

}  // namespace

zx_status_t EffectsLoader::CreateWithModule(const char* lib_name,
                                            std::unique_ptr<EffectsLoader>* out) {
  TRACE_DURATION("audio", "EffectsLoader::CreateWithModule");
  auto module = EffectsModuleV1::Open(lib_name);
  if (!module) {
    return ZX_ERR_UNAVAILABLE;
  }
  *out = std::unique_ptr<EffectsLoader>(new EffectsLoader(std::move(module)));
  return ZX_OK;
}

std::unique_ptr<EffectsLoader> EffectsLoader::CreateWithNullModule() {
  // Note we use a no-op 'release' method here since we're wrapping a static/const data member we
  // want to make sure we don't free this pointer.
  auto module = std::shared_ptr<const fuchsia_audio_effects_module_v1>(&kNullEffectModuleV1,
                                                                       [](auto* ptr) {});
  return std::unique_ptr<EffectsLoader>(new EffectsLoader(EffectsModuleV1(std::move(module))));
}

uint32_t EffectsLoader::GetNumEffects() {
  FXL_DCHECK(module_);
  return module_->num_effects;
}

zx_status_t EffectsLoader::GetEffectInfo(uint32_t effect_id,
                                         fuchsia_audio_effects_description* desc) {
  TRACE_DURATION("audio", "EffectsLoader::GetEffectInfo");
  FXL_DCHECK(module_);

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
  TRACE_DURATION("audio", "EffectsLoader::CreateEffect");
  FXL_DCHECK(module_);

  if (effect_id >= module_->num_effects) {
    return {};
  }
  if (!module_->create_effect) {
    return {};
  }

  auto effects_handle = module_->create_effect(effect_id, frame_rate, channels_in, channels_out,
                                               config.data(), config.size());
  if (effects_handle == FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE) {
    return {};
  }
  return {effects_handle, module_};
}

}  // namespace media::audio
