// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECTS_LOADER_V1_H_
#define SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECTS_LOADER_V1_H_

#include <lib/media/audio/effects/audio_effects.h>
#include <zircon/types.h>

#include <string_view>
#include <vector>

#include "src/media/audio/lib/effects_loader/effect_v1.h"
#include "src/media/audio/lib/effects_loader/effects_module.h"

namespace media::audio {

//
// The following zx_status_t values are returned by these methods:
//    ZX_ERR_UNAVAILABLE    - shared library could not be opened/closed
//    ZX_ERR_ALREADY_EXISTS - shared library is already loaded
//
//    ZX_ERR_NOT_FOUND      - library export function could not be found/loaded
//    ZX_ERR_NOT_SUPPORTED  - library export function returned an error
//
//    ZX_ERR_INVALID_ARGS   - caller parameter was unexpectedly null
//    ZX_ERR_OUT_OF_RANGE   - caller parameter was too high or too low
//
class EffectsLoaderV1 {
 public:
  // Creates a effects loader by opening the loadable module specified by |lib_name|.
  static zx_status_t CreateWithModule(const char* lib_name, std::unique_ptr<EffectsLoaderV1>* out);

  // Creates a 'null' effects loader. That is a loader that cannot create any effects.
  static std::unique_ptr<EffectsLoaderV1> CreateWithNullModule();

  uint32_t GetNumEffects() const;
  zx_status_t GetEffectInfo(uint32_t effect_id,
                            fuchsia_audio_effects_description* effect_desc) const;

  EffectV1 CreateEffectByName(std::string_view name, std::string_view instance_name,
                              int32_t frame_rate, int32_t channels_in, int32_t channels_out,
                              std::string_view config) const;

  // TODO(dalesat): Remove when callers have been migrated.
  EffectV1 CreateEffectByName(std::string_view name, int32_t frame_rate, int32_t channels_in,
                              int32_t channels_out, std::string_view config) const;

  EffectV1 CreateEffect(uint32_t effect_id, std::string_view instance_name, int32_t frame_rate,
                        int32_t channels_in, int32_t channels_out, std::string_view config) const;

 private:
  EffectsLoaderV1(EffectsModuleV1 module,
                  std::vector<fuchsia_audio_effects_description> effect_infos);

  EffectsModuleV1 module_;
  std::vector<fuchsia_audio_effects_description> effect_infos_;
};

// TODO(fxbug.dev/80067): delete after updating vendored code.
using EffectsLoader = EffectsLoaderV1;

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECTS_LOADER_V1_H_
