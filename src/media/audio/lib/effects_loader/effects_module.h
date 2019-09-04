// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECTS_MODULE_H_
#define SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECTS_MODULE_H_

#include <lib/media/audio/effects/audio_effects.h>

#include <memory>

namespace media::audio {

namespace internal {

template <typename ModuleImpl>
class EffectsModule {
 public:
  static EffectsModule<ModuleImpl> Open(const char* name);

  EffectsModule() = default;
  explicit EffectsModule(std::shared_ptr<const ModuleImpl> module);

  // Allow both move and copy. Defaults are fine here as a single shared_ptr is our only data
  // member.
  EffectsModule(const EffectsModule&) = default;
  EffectsModule& operator=(const EffectsModule&) = default;
  EffectsModule(EffectsModule&&) noexcept = default;
  EffectsModule& operator=(EffectsModule&&) noexcept = default;

  // Provide access to the underlying module structure.
  const ModuleImpl& operator*() const { return *module_; }
  const ModuleImpl* operator->() const { return module_.get(); }

  [[nodiscard]] bool is_valid() const { return static_cast<bool>(module_); }
  explicit operator bool() const { return is_valid(); }

  // Releases the reference to the module. After a call to |Release| the |EffectsModule| will be in
  // an invalid state (that is |is_valid| will return false).
  void Release() { module_ = nullptr; }
  EffectsModule& operator=(std::nullptr_t) {
    Release();
    return *this;
  }

 private:
  std::shared_ptr<const ModuleImpl> module_;
};

}  // namespace internal

using EffectsModuleV1 = internal::EffectsModule<fuchsia_audio_effects_module_v1>;

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECTS_MODULE_H_
