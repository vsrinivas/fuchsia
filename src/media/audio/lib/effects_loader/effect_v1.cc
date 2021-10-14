// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/effects_loader/effect_v1.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

namespace media::audio {

EffectV1::~EffectV1() {
  if (is_valid()) {
    Delete();
  }
}

// Allow move.
EffectV1::EffectV1(EffectV1&& o) noexcept
    : effects_handle_(o.effects_handle_),
      module_(std::move(o.module_)),
      instance_name_(std::move(o.instance_name_)) {
  o.effects_handle_ = FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE;
}
EffectV1& EffectV1::operator=(EffectV1&& o) noexcept {
  effects_handle_ = o.effects_handle_;
  module_ = std::move(o.module_);
  instance_name_ = std::move(o.instance_name_);
  o.effects_handle_ = FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE;
  return *this;
}

zx_status_t EffectV1::Delete() {
  TRACE_DURATION("audio", "EffectV1::Delete");
  FX_DCHECK(module_);
  FX_DCHECK(effects_handle_ != FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);
  auto result = module_->delete_effect(effects_handle_) ? ZX_OK : ZX_ERR_NOT_SUPPORTED;
  module_ = nullptr;
  effects_handle_ = FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE;
  return result;
}

zx_status_t EffectV1::UpdateConfiguration(std::string_view config) const {
  TRACE_DURATION("audio", "EffectV1::UpdateConfiguration");
  FX_DCHECK(module_);
  FX_DCHECK(effects_handle_ != FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);
  return module_->update_effect_configuration(effects_handle_, config.data(), config.size())
             ? ZX_OK
             : ZX_ERR_NOT_SUPPORTED;
}

zx_status_t EffectV1::ProcessInPlace(int64_t num_frames, float* audio_buff_in_out) const {
  TRACE_DURATION("audio", "EffectV1::ProcessInPlace", "num_frames", num_frames);
  FX_DCHECK(module_);
  FX_DCHECK(effects_handle_ != FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);
  return module_->process_inplace(effects_handle_, static_cast<uint32_t>(num_frames),
                                  audio_buff_in_out)
             ? ZX_OK
             : ZX_ERR_NOT_SUPPORTED;
}

zx_status_t EffectV1::Process(int64_t num_frames, const float* audio_buff_in,
                              float** audio_buff_out) const {
  TRACE_DURATION("audio", "EffectV1::Process", "num_frames", num_frames);
  FX_DCHECK(module_);
  FX_DCHECK(effects_handle_ != FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);
  return module_->process(effects_handle_, static_cast<uint32_t>(num_frames), audio_buff_in,
                          audio_buff_out)
             ? ZX_OK
             : ZX_ERR_NOT_SUPPORTED;
}

zx_status_t EffectV1::Flush() const {
  TRACE_DURATION("audio", "EffectV1::Flush");
  FX_DCHECK(module_);
  FX_DCHECK(effects_handle_ != FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);
  return module_->flush(effects_handle_) ? ZX_OK : ZX_ERR_NOT_SUPPORTED;
}

zx_status_t EffectV1::GetParameters(fuchsia_audio_effects_parameters* params) const {
  TRACE_DURATION("audio", "EffectV1::GetParameters");
  FX_DCHECK(module_);
  FX_DCHECK(effects_handle_ != FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);
  return module_->get_parameters(effects_handle_, params) ? ZX_OK : ZX_ERR_NOT_SUPPORTED;
}

void EffectV1::SetStreamInfo(const fuchsia_audio_effects_stream_info& stream_info) const {
  TRACE_DURATION("audio", "EffectV1::SetStreamInfo");
  FX_DCHECK(module_);
  FX_DCHECK(effects_handle_ != FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE);

  // If a module does not implement this we just don't notify them.
  if (module_->set_stream_info) {
    module_->set_stream_info(effects_handle_, &stream_info);
  }
}

}  // namespace media::audio
