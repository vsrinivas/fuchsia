// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECTS_LOADER_H_
#define SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECTS_LOADER_H_

#include <lib/media/audio/effects/audio_effects.h>
#include <zircon/types.h>

#include <string_view>

#include "src/media/audio/lib/effects_loader/effect.h"
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
class EffectsLoader {
 public:
  // Creates a effects loader by opening the loadable module specified by |lib_name|.
  static zx_status_t CreateWithModule(const char* lib_name, std::unique_ptr<EffectsLoader>* out);

  // Creates a 'null' effects loader. That is a loader that cannot create any effects.
  static std::unique_ptr<EffectsLoader> CreateWithNullModule();

  uint32_t GetNumEffects();
  zx_status_t GetEffectInfo(uint32_t effect_id, fuchsia_audio_effects_description* effect_desc);

  Effect CreateEffect(uint32_t effect_id, uint32_t frame_rate, uint16_t channels_in,
                      uint16_t channels_out, std::string_view config);

 private:
  explicit EffectsLoader(EffectsModuleV1 module) : module_(std::move(module)) {}

  EffectsModuleV1 module_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECTS_LOADER_H_
