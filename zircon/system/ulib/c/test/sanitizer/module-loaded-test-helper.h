// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_C_TEST_SANITIZER_MODULE_LOADED_TEST_HELPER_H_
#define ZIRCON_SYSTEM_ULIB_C_TEST_SANITIZER_MODULE_LOADED_TEST_HELPER_H_

#include <string.h>

extern "C" {

// State machine for knowing which part in startup we're in.
enum StartupOperations {
  kNothing,
  kRanModuleLoaded,
  kRanStartupHook,
  kRanModuleCtors,
};
extern StartupOperations gOperation;

// Number of newly loaded modules.
extern size_t gNumNewMods;

// Number of modules loaded in total.
extern size_t gNumLoadedMods;

}  // extern "C"

// This is the number of modules we expect to dlopen in
// module-loaded-test-helper.cc. The loaded modules should be:
// - libsanitizer-module-loaded-test-dlopen-dso.so
// - libsanitizer-module-loaded-test-dlopen-needed-dso.so
constexpr size_t kExpectedNumDlopenMods = 2;

#endif  // ZIRCON_SYSTEM_ULIB_C_TEST_SANITIZER_MODULE_LOADED_TEST_HELPER_H_
