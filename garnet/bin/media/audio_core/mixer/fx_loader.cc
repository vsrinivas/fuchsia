// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_core/mixer/fx_loader.h"

#include <dlfcn.h>

#include "src/lib/fxl/logging.h"

namespace media::audio {

// Private internal method
bool FxLoader::TryLoad(void* lib, const char* export_name,
                       void** export_func_ptr) {
  FXL_DCHECK(lib != nullptr);
  FXL_DCHECK(export_name != nullptr);
  FXL_DCHECK(export_func_ptr != nullptr);

  *export_func_ptr = dlsym(lib, export_name);
  if (*export_func_ptr == nullptr) {
    FXL_LOG(ERROR) << "Failed to load .SO export [" << export_name << "]";
    return false;
  }

  return true;
}

void FxLoader::ClearExports() {
  exports_loaded_ = false;

  fn_get_num_fx_ = nullptr;
  fn_get_info_ = nullptr;
  fn_get_ctrl_info_ = nullptr;

  fn_create_ = nullptr;
  fn_delete_ = nullptr;
  fn_get_params_ = nullptr;

  fn_get_ctrl_val_ = nullptr;
  fn_set_ctrl_val_ = nullptr;
  fn_reset_ = nullptr;

  fn_process_inplace_ = nullptr;
  fn_process_ = nullptr;
  fn_flush_ = nullptr;
}

//
// Protected methods
//
// Virtual, can be overridden by children (test fixtures)
void* FxLoader::OpenLoadableModuleBinary() {
  auto module = dlopen("audiofx.so", RTLD_LAZY | RTLD_GLOBAL);
  if (module == nullptr) {
    FXL_LOG(ERROR) << "audiofx.so did not load";
  }
  return module;
}

//
// Public methods
//
// TODO(mpuryear): Consider moving to a single export symbol, which in turn
// returns the function pointers that I currently load/check individually.
zx_status_t FxLoader::LoadLibrary() {
  if (fx_lib_ != nullptr) {
    return ZX_ERR_ALREADY_EXISTS;
  }

  fx_lib_ = OpenLoadableModuleBinary();
  if (fx_lib_ == nullptr) {
    return ZX_ERR_UNAVAILABLE;
  }

  bool load_success = true;
  // Don't stop at the first failure; try to load ALL the exports. The &&=
  // operator "short-circuits" (exits early), so we use bitwise &= instead.
  load_success &= TryLoad(fx_lib_, "fuchsia_audio_dfx_get_num_effects",
                          reinterpret_cast<void**>(&fn_get_num_fx_));
  load_success &= TryLoad(fx_lib_, "fuchsia_audio_dfx_get_info",
                          reinterpret_cast<void**>(&fn_get_info_));
  load_success &= TryLoad(fx_lib_, "fuchsia_audio_dfx_get_control_info",
                          reinterpret_cast<void**>(&fn_get_ctrl_info_));

  load_success &= TryLoad(fx_lib_, "fuchsia_audio_dfx_create",
                          reinterpret_cast<void**>(&fn_create_));
  load_success &= TryLoad(fx_lib_, "fuchsia_audio_dfx_delete",
                          reinterpret_cast<void**>(&fn_delete_));
  load_success &= TryLoad(fx_lib_, "fuchsia_audio_dfx_get_parameters",
                          reinterpret_cast<void**>(&fn_get_params_));

  load_success &= TryLoad(fx_lib_, "fuchsia_audio_dfx_get_control_value",
                          reinterpret_cast<void**>(&fn_get_ctrl_val_));
  load_success &= TryLoad(fx_lib_, "fuchsia_audio_dfx_set_control_value",
                          reinterpret_cast<void**>(&fn_set_ctrl_val_));
  load_success &= TryLoad(fx_lib_, "fuchsia_audio_dfx_reset",
                          reinterpret_cast<void**>(&fn_reset_));

  load_success &= TryLoad(fx_lib_, "fuchsia_audio_dfx_process_inplace",
                          reinterpret_cast<void**>(&fn_process_inplace_));
  load_success &= TryLoad(fx_lib_, "fuchsia_audio_dfx_process",
                          reinterpret_cast<void**>(&fn_process_));
  load_success &= TryLoad(fx_lib_, "fuchsia_audio_dfx_flush",
                          reinterpret_cast<void**>(&fn_flush_));

  if (!load_success) {
    ClearExports();
    return ZX_ERR_NOT_FOUND;
  }

  exports_loaded_ = true;

  // Pre-fetch this lib's number of effects. This can be 0, but shouldn't fail.
  if (fn_get_num_fx_(&num_fx_) == false) {
    num_fx_ = 0;
    return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

// TODO(mpuryear): dlfcn doesn't actually unload anything currently. Should we
// consider adding additional .SO entry points for Initialize and Deinitialize,
// so we can better control when the library does its resource allocation?
//
// Related: once we add FxProcessor, we must make sure to release any remaining
// FxProcessor instances here, before calling dlclose.
zx_status_t FxLoader::UnloadLibrary() {
  zx_status_t ret_val = ZX_OK;

  if (fx_lib_ == nullptr || dlclose(fx_lib_) != 0) {
    ret_val = ZX_ERR_UNAVAILABLE;
  }

  ClearExports();
  fx_lib_ = nullptr;

  return ret_val;
}

zx_status_t FxLoader::GetNumFx(uint32_t* num_fx_out) {
  if (!exports_loaded_) {
    return ZX_ERR_NOT_FOUND;
  }
  if (num_fx_out == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  *num_fx_out = num_fx_;
  return ZX_OK;
}

zx_status_t FxLoader::GetFxInfo(uint32_t effect_id,
                                fuchsia_audio_dfx_description* fx_desc) {
  if (!exports_loaded_) {
    return ZX_ERR_NOT_FOUND;
  }
  if (fx_desc == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (effect_id >= num_fx_) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (fn_get_info_(effect_id, fx_desc) == false) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

zx_status_t FxLoader::GetFxControlInfo(
    uint32_t effect_id, uint16_t ctrl_num,
    fuchsia_audio_dfx_control_description* fx_ctrl_desc) {
  if (!exports_loaded_) {
    return ZX_ERR_NOT_FOUND;
  }
  if (fx_ctrl_desc == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (effect_id >= num_fx_) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (fn_get_ctrl_info_(effect_id, ctrl_num, fx_ctrl_desc) == false) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

fx_token_t FxLoader::CreateFx(uint32_t effect_id, uint32_t frame_rate,
                              uint16_t channels_in, uint16_t channels_out) {
  if (!exports_loaded_) {
    return FUCHSIA_AUDIO_DFX_INVALID_TOKEN;
  }
  if (effect_id >= num_fx_) {
    return FUCHSIA_AUDIO_DFX_INVALID_TOKEN;
  }

  return fn_create_(effect_id, frame_rate, channels_in, channels_out);
}

zx_status_t FxLoader::DeleteFx(fx_token_t fx_token) {
  if (!exports_loaded_) {
    return ZX_ERR_NOT_FOUND;
  }
  if (fx_token == FUCHSIA_AUDIO_DFX_INVALID_TOKEN) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (fn_delete_(fx_token) == false) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

zx_status_t FxLoader::FxGetParameters(fx_token_t fx_token,
                                      fuchsia_audio_dfx_parameters* fx_params) {
  if (!exports_loaded_) {
    return ZX_ERR_NOT_FOUND;
  }
  if (fx_token == FUCHSIA_AUDIO_DFX_INVALID_TOKEN || fx_params == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (fn_get_params_(fx_token, fx_params) == false) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

zx_status_t FxLoader::FxGetControlValue(fx_token_t fx_token, uint16_t ctrl_num,
                                        float* ctrl_val_out) {
  if (!exports_loaded_) {
    return ZX_ERR_NOT_FOUND;
  }
  if (fx_token == FUCHSIA_AUDIO_DFX_INVALID_TOKEN || ctrl_val_out == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (fn_get_ctrl_val_(fx_token, ctrl_num, ctrl_val_out) == false) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

zx_status_t FxLoader::FxSetControlValue(fx_token_t fx_token, uint16_t ctrl_num,
                                        float ctrl_val) {
  if (!exports_loaded_) {
    return ZX_ERR_NOT_FOUND;
  }
  if (fx_token == FUCHSIA_AUDIO_DFX_INVALID_TOKEN) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (fn_set_ctrl_val_(fx_token, ctrl_num, ctrl_val) == false) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

zx_status_t FxLoader::FxReset(fx_token_t fx_token) {
  if (!exports_loaded_) {
    return ZX_ERR_NOT_FOUND;
  }
  if (fx_token == FUCHSIA_AUDIO_DFX_INVALID_TOKEN) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (fn_reset_(fx_token) == false) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

zx_status_t FxLoader::FxProcessInPlace(fx_token_t fx_token, uint32_t num_frames,
                                       float* audio_buff_in_out) {
  if (!exports_loaded_) {
    return ZX_ERR_NOT_FOUND;
  }
  if (fx_token == FUCHSIA_AUDIO_DFX_INVALID_TOKEN ||
      audio_buff_in_out == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (fn_process_inplace_(fx_token, num_frames, audio_buff_in_out) == false) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

zx_status_t FxLoader::FxProcess(fx_token_t fx_token, uint32_t num_frames,
                                const float* audio_buff_in,
                                float* audio_buff_out) {
  if (!exports_loaded_) {
    return ZX_ERR_NOT_FOUND;
  }
  if (fx_token == FUCHSIA_AUDIO_DFX_INVALID_TOKEN || audio_buff_in == nullptr ||
      audio_buff_out == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (fn_process_(fx_token, num_frames, audio_buff_in, audio_buff_out) ==
      false) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

zx_status_t FxLoader::FxFlush(fx_token_t fx_token) {
  if (!exports_loaded_) {
    return ZX_ERR_NOT_FOUND;
  }
  if (fx_token == FUCHSIA_AUDIO_DFX_INVALID_TOKEN) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (fn_flush_(fx_token) == false) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

}  // namespace media::audio
