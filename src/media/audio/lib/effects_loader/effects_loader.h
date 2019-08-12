// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECTS_LOADER_H_
#define SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECTS_LOADER_H_

#include <lib/media/audio/effects/audio_effects.h>
#include <zircon/types.h>

#include <string_view>

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

  ~EffectsLoader() { (void)UnloadLibrary(); }

  zx_status_t LoadLibrary();
  zx_status_t UnloadLibrary();

  // The following methods map directly to SO exports
  zx_status_t GetNumFx(uint32_t* num_effects_out);
  zx_status_t GetFxInfo(uint32_t effect_id, fuchsia_audio_effects_description* fx_desc);
  fuchsia_audio_effects_handle_t CreateFx(uint32_t effect_id, uint32_t frame_rate,
                                          uint16_t channels_in, uint16_t channels_out,
                                          std::string_view config);
  zx_status_t FxUpdateConfiguration(fuchsia_audio_effects_handle_t handle, std::string_view config);
  zx_status_t DeleteFx(fuchsia_audio_effects_handle_t handle);
  zx_status_t FxGetParameters(fuchsia_audio_effects_handle_t handle,
                              fuchsia_audio_effects_parameters* fx_params);
  zx_status_t FxProcessInPlace(fuchsia_audio_effects_handle_t handle, uint32_t num_frames,
                               float* audio_buff_in_out);
  zx_status_t FxProcess(fuchsia_audio_effects_handle_t handle, uint32_t num_frames,
                        const float* audio_buff_in, float* audio_buff_out);
  zx_status_t FxFlush(fuchsia_audio_effects_handle_t handle);

 protected:
  virtual void* OpenLoadableModuleBinary();

 private:
  const char* lib_name_;
  void* fx_lib_ = nullptr;
  fuchsia_audio_effects_module_v1* module_ = nullptr;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECTS_LOADER_H_
