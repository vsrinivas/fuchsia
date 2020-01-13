// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/effects_loader/effects_module.h"

#include <dlfcn.h>

#include "src/lib/syslog/cpp/logger.h"

namespace media::audio::internal {
namespace {

bool TryLoad(void* lib, const char* export_name, void** export_ptr) {
  FX_DCHECK(lib != nullptr);
  FX_DCHECK(export_name != nullptr);
  FX_DCHECK(export_ptr != nullptr);

  *export_ptr = dlsym(lib, export_name);
  if (*export_ptr == nullptr) {
    FX_LOGS(ERROR) << "Failed to load .SO export [" << export_name << "]";
    return false;
  }

  return true;
}

template <typename ModuleImpl>
constexpr const char* ExportSymbolName() = delete;
template <>
constexpr const char* ExportSymbolName<fuchsia_audio_effects_module_v1>() {
  return "fuchsia_audio_effects_module_v1_instance";
}

}  // namespace

template <typename ModuleImpl>
EffectsModule<ModuleImpl> EffectsModule<ModuleImpl>::Open(const char* name) {
  void* lib = dlopen(name, RTLD_LAZY | RTLD_GLOBAL);
  if (lib == nullptr) {
    FX_LOGS(ERROR) << "Failed to open '" << name << "' " << dlerror();
    return {};
  }
  ModuleImpl* module = nullptr;
  if (!TryLoad(lib, ExportSymbolName<ModuleImpl>(), reinterpret_cast<void**>(&module))) {
    return {};
  }
  // We use the shared_ptr to handle the basic ref counting. We provide a custom deleter as we
  // don't actually own the module pointer (it's just some data within the loaded module), but
  // instead we want to unload the module when the refcount reaches 0.
  return EffectsModule<ModuleImpl>(std::shared_ptr<const ModuleImpl>(
      module, [lib](ModuleImpl* ptr) { FX_DCHECK(dlclose(lib) == 0); }));
}

template <typename ModuleImpl>
EffectsModule<ModuleImpl>::EffectsModule(std::shared_ptr<const ModuleImpl> module)
    : module_(std::move(module)) {}

template class EffectsModule<fuchsia_audio_effects_module_v1>;

}  // namespace media::audio::internal
