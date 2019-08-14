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
  EffectsLoader(const char* lib_name) : lib_name_(lib_name) {}
  EffectsLoader() : EffectsLoader("audio_effects.so") {}

  // Attempts to open the shared library that contains audio effects. This shared library will
  // remain open until both `EffectsLoader` is destructed, and _all_ `Effect`s created with this
  // `EffectsLoader` are destructed.
  //
  // It is safe to free this object after the `Effect`s have been created, each `Effect` will
  // retain a reference to the underlying shared library. Once all references to the shared library
  // are released, the library will be closed.
  zx_status_t LoadLibrary();

  Effect CreateEffect(uint32_t effect_id, uint32_t frame_rate, uint16_t channels_in,
                      uint16_t channels_out, std::string_view config);

  zx_status_t GetNumFx(uint32_t* num_effects_out);
  zx_status_t GetFxInfo(uint32_t effect_id, fuchsia_audio_effects_description* fx_desc);

 private:
  const char* lib_name_;
  EffectsModuleV1 module_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECTS_LOADER_H_
