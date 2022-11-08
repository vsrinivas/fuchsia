// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>
#include <zircon/compiler.h>

#include "module-loaded-test-helper.h"

__EXPORT __WEAK StartupOperations gOperation;
__EXPORT __WEAK size_t gNumNewMods;
__EXPORT __WEAK size_t gNumLoadedMods;

__attribute__((constructor)) void ModuleConstructor() {
  // Assert this runs only after the module loaded hook is called.
  ZX_ASSERT_MSG(gOperation == kRanModuleLoaded, "__sanitizer_module_loaded was not run");
  ZX_ASSERT_MSG(gNumNewMods == kExpectedNumDlopenMods,
                "The expected number of modules were not loaded before this ctor was called");
}
