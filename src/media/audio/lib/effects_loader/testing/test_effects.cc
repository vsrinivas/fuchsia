// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/effects_loader/testing/test_effects.h"

#include <dlfcn.h>

#include "src/media/audio/effects/test_effects/test_effects.h"

namespace media::audio::testing {

std::shared_ptr<test_effects_module_ext> OpenTestEffectsExt() {
  void* lib = dlopen(kTestEffectsModuleName, RTLD_GLOBAL | RTLD_LAZY);
  if (!lib) {
    return nullptr;
  }
  void* sym = dlsym(lib, "test_effects_module_ext_instance");
  if (!sym) {
    return nullptr;
  }
  // Since the pointer is owned by the loaded module; we don't free() the pointer but instead just
  // dlclose the shared lib that owns the pointer.
  return std::shared_ptr<test_effects_module_ext>(reinterpret_cast<test_effects_module_ext*>(sym),
                                                  [lib](auto* ptr) { dlclose(lib); });
}

}  // namespace media::audio::testing
