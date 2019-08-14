// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/effects_loader/effects_loader_test_base.h"

#include <dlfcn.h>

#include "src/lib/fxl/logging.h"
#include "src/media/audio/effects/test_effects/test_effects.h"
#include "src/media/audio/lib/effects_loader/effects_processor.h"

namespace media::audio::test {

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

void EffectsLoaderTestBase::SetUp() {
  testing::Test::SetUp();
  ASSERT_EQ(effects_loader_.LoadLibrary(), ZX_OK);
  test_effects_ = OpenTestEffectsExt();
  ASSERT_TRUE(test_effects_ != nullptr);
}

void EffectsLoaderTestBase::TearDown() {
  ASSERT_TRUE(test_effects_ != nullptr);
  ASSERT_EQ(0u, test_effects_->num_instances());
  ASSERT_EQ(ZX_OK, test_effects_->clear_effects());
  ASSERT_EQ(effects_loader_.UnloadLibrary(), ZX_OK);
  testing::Test::TearDown();
}

}  // namespace media::audio::test
