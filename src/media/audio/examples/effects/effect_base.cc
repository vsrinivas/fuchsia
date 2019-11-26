// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/examples/effects/effect_base.h"

#include <cmath>

#include <fbl/algorithm.h>

#include "src/media/audio/examples/effects/delay_effect.h"
#include "src/media/audio/examples/effects/rechannel_effect.h"
#include "src/media/audio/examples/effects/swap_effect.h"

namespace media::audio_effects_example {

//
// EffectBase: static member functions
//

// static; satisfied by base class
bool EffectBase::GetNumEffects(uint32_t* num_effects_out) {
  *num_effects_out = Effect::Count;
  return true;
}

// static; dispatched by base class to the appropriate subclass static
bool EffectBase::GetInfo(uint32_t effect_id, fuchsia_audio_effects_description* desc) {
  switch (effect_id) {
    case media::audio_effects_example::Effect::Delay:
      return media::audio_effects_example::DelayEffect::GetInfo(desc);
    case media::audio_effects_example::Effect::Rechannel:
      return media::audio_effects_example::RechannelEffect::GetInfo(desc);
    case media::audio_effects_example::Effect::Swap:
      return media::audio_effects_example::SwapEffect::GetInfo(desc);
  }

  return false;
}

// static; dispatched by base class to the appropriate subclass static
EffectBase* EffectBase::Create(uint32_t effect_id, uint32_t frame_rate, uint16_t channels_in,
                               uint16_t channels_out, std::string_view config) {
  if (channels_in > FUCHSIA_AUDIO_EFFECTS_CHANNELS_MAX ||
      channels_out > FUCHSIA_AUDIO_EFFECTS_CHANNELS_MAX) {
    return nullptr;
  }

  switch (effect_id) {
    case media::audio_effects_example::Effect::Delay:
      return reinterpret_cast<EffectBase*>(
          DelayEffect::Create(frame_rate, channels_in, channels_out, config));

    case media::audio_effects_example::Effect::Rechannel:
      return reinterpret_cast<EffectBase*>(
          RechannelEffect::Create(frame_rate, channels_in, channels_out, config));

    case media::audio_effects_example::Effect::Swap:
      return reinterpret_cast<EffectBase*>(
          SwapEffect::Create(frame_rate, channels_in, channels_out, config));
  }

  return nullptr;
}

//
// EffectBase: instance member functions
//
bool EffectBase::GetParameters(fuchsia_audio_effects_parameters* effect_params) {
  effect_params->frame_rate = frame_rate_;
  effect_params->channels_in = channels_in_;
  effect_params->channels_out = channels_out_;

  effect_params->signal_latency_frames = frames_latency_;
  effect_params->suggested_frames_per_buffer = suggested_buff_frames_;

  return true;
}

}  // namespace media::audio_effects_example
