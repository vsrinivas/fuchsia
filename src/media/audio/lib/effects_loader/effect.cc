// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/effects_loader/effect.h"

#include <lib/trace/event.h>

#include "src/lib/syslog/cpp/logger.h"

namespace media::audio {

Effect::~Effect() {
  if (is_valid()) {
    Delete();
  }
}

// Allow move.
Effect::Effect(Effect&& o) noexcept
    : effects_handle_(o.effects_handle_), module_(std::move(o.module_)) {
  o.effects_handle_ = FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE;
}
Effect& Effect::operator=(Effect&& o) noexcept {
  effects_handle_ = o.effects_handle_;
  module_ = std::move(o.module_);
  o.effects_handle_ = FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE;
  return *this;
}

zx_status_t Effect::Delete() {
  TRACE_DURATION("audio", "Effect::Delete");
  FX_DCHECK(module_);
  FX_DCHECK(effects_handle_ != FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);
  auto result = module_->delete_effect(effects_handle_) ? ZX_OK : ZX_ERR_NOT_SUPPORTED;
  module_ = nullptr;
  effects_handle_ = FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE;
  return result;
}

zx_status_t Effect::UpdateConfiguration(std::string_view config) const {
  TRACE_DURATION("audio", "Effect::UpdateConfiguration");
  FX_DCHECK(module_);
  FX_DCHECK(effects_handle_ != FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);
  return module_->update_effect_configuration(effects_handle_, config.data(), config.size())
             ? ZX_OK
             : ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Effect::ProcessInPlace(uint32_t num_frames, float* audio_buff_in_out) const {
  TRACE_DURATION("audio", "Effect::ProcessInPlace", "num_frames", num_frames);
  FX_DCHECK(module_);
  FX_DCHECK(effects_handle_ != FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);
  return module_->process_inplace(effects_handle_, num_frames, audio_buff_in_out)
             ? ZX_OK
             : ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Effect::Process(uint32_t num_frames, const float* audio_buff_in,
                            float** audio_buff_out) const {
  TRACE_DURATION("audio", "Effect::Process", "num_frames", num_frames);
  FX_DCHECK(module_);
  FX_DCHECK(effects_handle_ != FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);
  return module_->process(effects_handle_, num_frames, audio_buff_in, audio_buff_out)
             ? ZX_OK
             : ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Effect::Flush() const {
  TRACE_DURATION("audio", "Effect::Flush");
  FX_DCHECK(module_);
  FX_DCHECK(effects_handle_ != FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);
  return module_->flush(effects_handle_) ? ZX_OK : ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Effect::GetParameters(fuchsia_audio_effects_parameters* params) const {
  TRACE_DURATION("audio", "Effect::GetParameters");
  FX_DCHECK(module_);
  FX_DCHECK(effects_handle_ != FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);
  return module_->get_parameters(effects_handle_, params) ? ZX_OK : ZX_ERR_NOT_SUPPORTED;
}

}  // namespace media::audio
