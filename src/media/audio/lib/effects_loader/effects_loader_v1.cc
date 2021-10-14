// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/effects_loader/effects_loader_v1.h"

#include <dlfcn.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include <algorithm>
#include <optional>

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

std::optional<uint32_t> FindEffectIdForEffectName(
    std::string_view name, const std::vector<fuchsia_audio_effects_description>& effect_infos) {
  auto it = std::find_if(effect_infos.cbegin(), effect_infos.cend(),
                         [name](const auto& info) { return info.name == name; });
  if (it == effect_infos.cend()) {
    return std::nullopt;
  }
  return {it - effect_infos.cbegin()};
}

}  // namespace

zx_status_t EffectsLoaderV1::CreateWithModule(const char* lib_name,
                                              std::unique_ptr<EffectsLoaderV1>* out) {
  TRACE_DURATION("audio", "EffectsLoaderV1::CreateWithModule");
  auto module = EffectsModuleV1::Open(lib_name);
  if (!module) {
    return ZX_ERR_UNAVAILABLE;
  }
  std::vector<fuchsia_audio_effects_description> infos(module->num_effects);
  for (uint32_t effect_id = 0; effect_id < module->num_effects; ++effect_id) {
    if (!module->get_info(effect_id, &infos[effect_id])) {
      return ZX_ERR_INVALID_ARGS;
    }
  }
  *out = std::unique_ptr<EffectsLoaderV1>(new EffectsLoaderV1(std::move(module), std::move(infos)));
  return ZX_OK;
}

std::unique_ptr<EffectsLoaderV1> EffectsLoaderV1::CreateWithNullModule() {
  // Note we use a no-op 'release' method here since we're wrapping a static/const data member we
  // want to make sure we don't free this pointer.
  auto module = std::shared_ptr<const fuchsia_audio_effects_module_v1>(&kNullEffectModuleV1,
                                                                       [](auto* ptr) {});
  return std::unique_ptr<EffectsLoaderV1>(
      new EffectsLoaderV1(EffectsModuleV1(std::move(module)), {}));
}

EffectsLoaderV1::EffectsLoaderV1(EffectsModuleV1 module,
                                 std::vector<fuchsia_audio_effects_description> effect_infos)
    : module_(std::move(module)), effect_infos_(std::move(effect_infos)) {
  FX_CHECK(module_->num_effects == effect_infos_.size());
}

uint32_t EffectsLoaderV1::GetNumEffects() const {
  FX_DCHECK(module_);
  return module_->num_effects;
}

zx_status_t EffectsLoaderV1::GetEffectInfo(uint32_t effect_id,
                                           fuchsia_audio_effects_description* desc) const {
  TRACE_DURATION("audio", "EffectsLoaderV1::GetEffectInfo");
  FX_DCHECK(module_);

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

EffectV1 EffectsLoaderV1::CreateEffectByName(std::string_view name, std::string_view instance_name,
                                             int32_t frame_rate, int32_t channels_in,
                                             int32_t channels_out, std::string_view config) const {
  TRACE_DURATION("audio", "EffectsLoaderV1::CreateEffectByName");

  auto effect_id = FindEffectIdForEffectName(name, effect_infos_);
  if (!effect_id) {
    FX_LOGS(ERROR) << "Effect " << name << " with name " << instance_name
                   << " unable to be created: effect id not found.";
    return {};
  }
  return CreateEffect(*effect_id, instance_name, frame_rate, static_cast<uint16_t>(channels_in),
                      static_cast<uint16_t>(channels_out), config);
}

EffectV1 EffectsLoaderV1::CreateEffectByName(std::string_view name, int32_t frame_rate,
                                             int32_t channels_in, int32_t channels_out,
                                             std::string_view config) const {
  TRACE_DURATION("audio", "EffectsLoaderV1::CreateEffectByName");

  auto effect_id = FindEffectIdForEffectName(name, effect_infos_);
  if (!effect_id) {
    FX_LOGS(ERROR) << "Effect " << name << " unable to be created: effect id not found.";
    return {};
  }
  return CreateEffect(*effect_id, "", frame_rate, static_cast<uint16_t>(channels_in),
                      static_cast<uint16_t>(channels_out), config);
}

EffectV1 EffectsLoaderV1::CreateEffect(uint32_t effect_id, std::string_view instance_name,
                                       int32_t frame_rate, int32_t channels_in,
                                       int32_t channels_out, std::string_view config) const {
  TRACE_DURATION("audio", "EffectsLoaderV1::CreateEffect");
  FX_DCHECK(module_);

  if (effect_id >= module_->num_effects) {
    return {};
  }
  if (!module_->create_effect) {
    return {};
  }

  auto effects_handle =
      module_->create_effect(effect_id, frame_rate, static_cast<uint16_t>(channels_in),
                             static_cast<uint16_t>(channels_out), config.data(), config.size());
  if (effects_handle == FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE) {
    return {};
  }
  return {effects_handle, module_, instance_name};
}

}  // namespace media::audio
