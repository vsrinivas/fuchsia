// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECT_H_
#define SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECT_H_

#include <lib/media/audio/effects/audio_effects.h>
#include <zircon/types.h>

#include <string_view>

#include "src/lib/syslog/cpp/logger.h"
#include "src/media/audio/lib/effects_loader/effects_module.h"

namespace media::audio {

class Effect {
 public:
  Effect() : effects_handle_(FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE) {}

  // Creates a new `Effect` from a `fuchsia_audio_effects_handle_t` and an owning
  // `EffectsModuleV1`.
  //
  // This constructor requires that both `handle` and `module` are both either vaild or invalid
  // values. It is an error to create an `Effect` with `handle` ==
  // `FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE` while `module` is non-null. Likewise it is an error to
  // create an `Effect` with `handle` != `FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE` and a null
  // `module`.
  Effect(fuchsia_audio_effects_handle_t effects_handle, EffectsModuleV1 module)
      : effects_handle_(effects_handle), module_(std::move(module)) {
    // If handle_ is valid, module_ must be valid. If effects_handle_ is invalid, module_ must be
    // invalid.
    FX_DCHECK((effects_handle_ != FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE) == (module_.is_valid()));
  }

  ~Effect();

  // Allow move.
  Effect(Effect&& o) noexcept;
  Effect& operator=(Effect&& o) noexcept;

  // Disallow copy.
  Effect(const Effect&) = delete;
  Effect& operator=(const Effect&) = delete;

  // Returns |true| iff this Effect has a valid fuchsia_audio_effects_handle_t.
  [[nodiscard]] bool is_valid() const { return static_cast<bool>(module_); }
  explicit operator bool() const { return is_valid(); }

  [[nodiscard]] fuchsia_audio_effects_handle_t get() const { return effects_handle_; }

  // These methods are thin wrappers around the corresponding ABI calls that use the
  // fuchsia_audio_effects_handle_t and module used to create this effect. It is an error to call
  // any of these if the Effect instance is not valid (see |is_valid|).
  //
  // In the spirit of keeping these as thin wrappers around the fuchsia_audio_effects_handle_t,
  // this class will not perform any parameter checking; all arguments will be passed through to
  // the plugin as-is.

  // Deletes the `Effect` leaving the object in an invalid state.
  //
  // Note that this will invalidate the `Effect` even if the operation fails.
  zx_status_t Delete();
  zx_status_t UpdateConfiguration(std::string_view config) const;
  zx_status_t ProcessInPlace(uint32_t num_frames, float* audio_buff_in_out) const;
  zx_status_t Process(uint32_t num_frames, const float* audio_buff_in,
                      float** audio_buff_out) const;
  zx_status_t Flush() const;
  zx_status_t GetParameters(fuchsia_audio_effects_parameters* params) const;

 private:
  fuchsia_audio_effects_handle_t effects_handle_;
  EffectsModuleV1 module_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECT_H_
