// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECT_V1_H_
#define SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECT_V1_H_

#include <lib/media/audio/effects/audio_effects.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/types.h>

#include <string_view>

#include "src/media/audio/lib/effects_loader/effects_module.h"

namespace media::audio {

class EffectV1 {
 public:
  EffectV1() : effects_handle_(FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE) {}

  // Creates a new `EffectV1` from a `fuchsia_audio_effects_handle_t` and an owning
  // `EffectsModuleV1`.
  //
  // This constructor requires that both `handle` and `module` are both either vaild or invalid
  // values. It is an error to create an `EffectV1` with `handle` ==
  // `FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE` while `module` is non-null. Likewise it is an error to
  // create an `EffectV1` with `handle` != `FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE` and a null
  // `module`.
  EffectV1(fuchsia_audio_effects_handle_t effects_handle, EffectsModuleV1 module,
           std::string_view instance_name)
      : effects_handle_(effects_handle), module_(std::move(module)), instance_name_(instance_name) {
    // If handle_ is valid, module_ must be valid. If effects_handle_ is invalid, module_ must be
    // invalid.
    FX_DCHECK((effects_handle_ != FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE) == (module_.is_valid()));
  }

  ~EffectV1();

  // Allow move.
  EffectV1(EffectV1&& o) noexcept;
  EffectV1& operator=(EffectV1&& o) noexcept;

  // Disallow copy.
  EffectV1(const EffectV1&) = delete;
  EffectV1& operator=(const EffectV1&) = delete;

  // Returns |true| iff this EffectV1 has a valid fuchsia_audio_effects_handle_t.
  [[nodiscard]] bool is_valid() const { return static_cast<bool>(module_); }
  explicit operator bool() const { return is_valid(); }

  [[nodiscard]] fuchsia_audio_effects_handle_t get() const { return effects_handle_; }

  std::string_view instance_name() const { return instance_name_; }

  // These methods are thin wrappers around the corresponding ABI calls that use the
  // fuchsia_audio_effects_handle_t and module used to create this effect. It is an error to call
  // any of these if the EffectV1 instance is not valid (see |is_valid|).
  //
  // In the spirit of keeping these as thin wrappers around the fuchsia_audio_effects_handle_t,
  // this class will not perform any parameter checking; all arguments will be passed through to
  // the plugin as-is.

  // Deletes the `EffectV1` leaving the object in an invalid state.
  //
  // Note that this will invalidate the `EffectV1` even if the operation fails.
  zx_status_t Delete();
  zx_status_t UpdateConfiguration(std::string_view config) const;
  zx_status_t ProcessInPlace(int64_t num_frames, float* audio_buff_in_out) const;
  zx_status_t Process(int64_t num_frames, const float* audio_buff_in, float** audio_buff_out) const;
  zx_status_t Flush() const;
  zx_status_t GetParameters(fuchsia_audio_effects_parameters* params) const;
  void SetStreamInfo(const fuchsia_audio_effects_stream_info& stream_info) const;

 private:
  fuchsia_audio_effects_handle_t effects_handle_;
  EffectsModuleV1 module_;
  std::string_view instance_name_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECT_V1_H_
