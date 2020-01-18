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

std::pair<int64_t, uint32_t> AlignBufferRequest(int64_t frame, uint32_t length,
                                                uint32_t alignment) {
  auto mask = ~(alignment - 1);
  auto aligned_frame = frame & mask;
  auto aligned_length = (length + alignment - 1) & mask;
  return {aligned_frame, aligned_length};
}

}  // namespace

// static
std::shared_ptr<EffectsStage> EffectsStage::Create(
    const std::vector<PipelineConfig::Effect>& effects, std::shared_ptr<Stream> source) {
  TRACE_DURATION("audio", "EffectsStage::Create");
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
  TRACE_DURATION("audio", "EffectsStage::LockBuffer", "frame", frame, "length", frame_count);
  // If we have a partially consumed block, return that here.
  if (current_block_ && frame >= current_block_->start() && frame < current_block_->end()) {
    return current_block_;
  }

  // New frames are requested. Block-align the start frame and length.
  auto [aligned_first_frame, aligned_frame_count] =
      AlignBufferRequest(frame, frame_count, effects_processor_->block_size());
  current_block_ = source_->LockBuffer(ref_time, aligned_first_frame, aligned_frame_count);
  if (current_block_) {
    auto num_frames = current_block_->length().Floor();
    FX_CHECK(num_frames == aligned_frame_count);
    FX_CHECK(current_block_->start().Floor() == aligned_first_frame);

    auto payload = static_cast<float*>(current_block_->payload());
    effects_processor_->ProcessInPlace(num_frames, payload);
  }
  return current_block_;
}

}  // namespace media::audio
