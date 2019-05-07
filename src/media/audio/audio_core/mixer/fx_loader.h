// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_FX_LOADER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_FX_LOADER_H_

#include <zircon/types.h>

#include "lib/media/audio_dfx/audio_device_fx.h"

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
class FxLoader {
 public:
  FxLoader() = default;
  ~FxLoader() { (void)UnloadLibrary(); }

  zx_status_t LoadLibrary();
  zx_status_t UnloadLibrary();

  // The following methods map directly to SO exports
  zx_status_t GetNumFx(uint32_t* num_effects_out);

  zx_status_t GetFxInfo(uint32_t effect_id,
                        fuchsia_audio_dfx_description* fx_desc);
  zx_status_t GetFxControlInfo(
      uint32_t effect_id, uint16_t ctrl_num,
      fuchsia_audio_dfx_control_description* fx_ctrl_desc);

  fx_token_t CreateFx(uint32_t effect_id, uint32_t frame_rate,
                      uint16_t channels_in, uint16_t channels_out);
  zx_status_t DeleteFx(fx_token_t fx_token);
  zx_status_t FxGetParameters(fx_token_t fx_token,
                              fuchsia_audio_dfx_parameters* fx_params);

  zx_status_t FxGetControlValue(fx_token_t fx_token, uint16_t ctrl_num,
                                float* value_out);
  zx_status_t FxSetControlValue(fx_token_t fx_token, uint16_t ctrl_num,
                                float value);
  zx_status_t FxReset(fx_token_t fx_token);

  zx_status_t FxProcessInPlace(fx_token_t fx_token, uint32_t num_frames,
                               float* audio_buff_in_out);
  zx_status_t FxProcess(fx_token_t fx_token, uint32_t num_frames,
                        const float* audio_buff_in, float* audio_buff_out);
  zx_status_t FxFlush(fx_token_t fx_token);

 protected:
  virtual void* OpenLoadableModuleBinary();

  bool exports_loaded_ = false;

 private:
  bool TryLoad(void* lib, const char* export_name, void** export_func_ptr);
  void ClearExports();

  void* fx_lib_ = nullptr;
  uint32_t num_fx_ = 0;

  bool (*fn_get_num_fx_)(uint32_t*);
  bool (*fn_get_info_)(uint32_t, fuchsia_audio_dfx_description*);
  bool (*fn_get_ctrl_info_)(uint32_t, uint16_t,
                            fuchsia_audio_dfx_control_description*);

  fx_token_t (*fn_create_)(uint32_t, uint32_t, uint16_t, uint16_t);
  bool (*fn_delete_)(fx_token_t);
  bool (*fn_get_params_)(fx_token_t, fuchsia_audio_dfx_parameters*);

  bool (*fn_get_ctrl_val_)(fx_token_t, uint16_t, float*);
  bool (*fn_set_ctrl_val_)(fx_token_t, uint16_t, float);
  bool (*fn_reset_)(fx_token_t);

  bool (*fn_process_inplace_)(fx_token_t, uint32_t, float*);
  bool (*fn_process_)(fx_token_t, uint32_t, const float*, float*);
  bool (*fn_flush_)(fx_token_t);
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_FX_LOADER_H_
