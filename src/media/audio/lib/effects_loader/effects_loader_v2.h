// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECTS_LOADER_V2_H_
#define SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECTS_LOADER_V2_H_

#include <fidl/fuchsia.audio.effects/cpp/wire.h>
#include <lib/fpromise/result.h>
#include <lib/sys/cpp/component_context.h>
#include <zircon/types.h>

#include <string>

namespace media::audio {

class EffectsLoaderV2 {
 public:
  // Creates a effects loader from the global namespace.
  static fpromise::result<std::unique_ptr<EffectsLoaderV2>, zx_status_t> CreateFromContext(
      const sys::ComponentContext& component_context);

  // Creates an effects loader from the given FIDL channel.
  static fpromise::result<std::unique_ptr<EffectsLoaderV2>, zx_status_t> CreateFromChannel(
      fidl::ClientEnd<fuchsia_audio_effects::ProcessorCreator> creator);

  // Get a ProcessorConfiguration for the effect with the given name.
  fidl::WireResult<fuchsia_audio_effects::ProcessorCreator::Create> GetProcessorConfiguration(
      std::string name);

 private:
  EffectsLoaderV2() = default;
  explicit EffectsLoaderV2(fidl::ClientEnd<fuchsia_audio_effects::ProcessorCreator> creator)
      : creator_(std::move(creator)) {}

  fidl::WireSyncClient<fuchsia_audio_effects::ProcessorCreator> creator_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECTS_LOADER_V2_H_
