// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/effects_stage.h"

#include "src/media/audio/lib/effects_loader/effects_loader.h"
#include "src/media/audio/lib/effects_loader/effects_processor.h"

namespace media::audio {
namespace {

class MultiLibEffectsLoader {
 public:
  Effect CreateEffectByName(std::string_view lib_name, std::string_view effect_name,
                            uint32_t frame_rate, uint16_t channels_in, uint16_t channels_out,
                            std::string_view config) {
    auto it = std::find_if(holders_.begin(), holders_.end(),
                           [lib_name](auto& holder) { return holder.lib_name == lib_name; });
    if (it == holders_.end()) {
      Holder holder;
      holder.lib_name = lib_name;
      zx_status_t status = EffectsLoader::CreateWithModule(holder.lib_name.c_str(), &holder.loader);
      if (status != ZX_OK) {
        return {};
      }
      it = holders_.insert(it, std::move(holder));
    }

    FX_CHECK(it != holders_.end());
    return it->loader->CreateEffectByName(effect_name, frame_rate, channels_in, channels_out,
                                          config);
  }

 private:
  struct Holder {
    std::string lib_name;
    std::unique_ptr<EffectsLoader> loader;
  };
  std::vector<Holder> holders_;
};

}  // namespace

// static
std::shared_ptr<EffectsStage> EffectsStage::Create(
    const std::vector<PipelineConfig::Effect>& effects, std::shared_ptr<Stream> source) {
  if (source->format().sample_format() != fuchsia::media::AudioSampleFormat::FLOAT) {
    FX_LOGS(ERROR) << "EffectsStage can only be added to streams with FLOAT samples";
    return nullptr;
  }

  auto processor = std::make_unique<EffectsProcessor>();

  MultiLibEffectsLoader loader;
  uint32_t frame_rate = source->format().frames_per_second();
  uint16_t channels = source->format().channels();
  for (const auto& effect_spec : effects) {
    auto effect =
        loader.CreateEffectByName(effect_spec.lib_name, effect_spec.effect_name, frame_rate,
                                  channels, channels, effect_spec.effect_config);
    FX_DCHECK(effect);
    if (!effect) {
      FX_LOGS(ERROR) << "Unable to create effect '" << effect_spec.effect_name << "' with config '"
                     << effect_spec.effect_config << "' from lib '" << effect_spec.lib_name << "'";
      continue;
    }
    processor->AddEffect(std::move(effect));
  }

  return std::make_shared<EffectsStage>(std::move(source), std::move(processor));
}

std::optional<Stream::Buffer> EffectsStage::LockBuffer(zx::time ref_time, int64_t frame,
                                                       uint32_t frame_count) {
  auto buffer = source_->LockBuffer(ref_time, frame, frame_count);
  if (buffer) {
    auto num_frames = buffer->length().Floor();
    auto payload = static_cast<float*>(buffer->payload());
    effects_processor_->ProcessInPlace(num_frames, payload);
  }
  return buffer;
}

}  // namespace media::audio
