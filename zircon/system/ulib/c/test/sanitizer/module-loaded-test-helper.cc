// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a self-containted executable that is not built with sanitizers or
// gtest. This is so we can explicitly define this hook that will run during
// startup. This can't be done in a normal gtest. This test is used in
// conjunction with the ModuleLoaded tests in sanitizer-utils which just check
// whether this program failed or not.
#include "module-loaded-test-helper.h"

#include <dlfcn.h>
#include <link.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/sanitizer.h>

#include <string_view>

__EXPORT StartupOperations gOperation = kNothing;
__EXPORT size_t gNumNewMods = 0;
__EXPORT size_t gNumLoadedMods;

namespace {

// Have a buffer large enough to hold all the phdr info found via the module
// loaded hook. It's unlikely we'll have more deps than this for this test.
constexpr size_t kBuffSize = 16;
dl_phdr_info gPhdrInfo[kBuffSize];

constexpr const char kDlopenDso[] = "libsanitizer-module-loaded-test-dlopen-dso.so";

bool FoundModule(const dl_phdr_info& check_info) {
  for (size_t i = 0; i < gNumLoadedMods; ++i) {
    const dl_phdr_info& found_info = gPhdrInfo[i];
    // Note we don't compare dlpi_adds/subs since they depend on global state which
    // we don't want to consider here.
    if (found_info.dlpi_addr == check_info.dlpi_addr &&
        found_info.dlpi_name == check_info.dlpi_name &&
        found_info.dlpi_phdr == check_info.dlpi_phdr &&
        found_info.dlpi_phnum == check_info.dlpi_phnum &&
        found_info.dlpi_tls_modid == check_info.dlpi_tls_modid &&
        found_info.dlpi_tls_data == check_info.dlpi_tls_data)
      return true;
  }
  return false;
}

void VerifyLoadedModules() {
  size_t num_found_mods = 0;
  dl_iterate_phdr(
      [](struct dl_phdr_info* info, size_t size, void* data) {
        size_t& num_found_mods = *reinterpret_cast<size_t*>(data);
        ++num_found_mods;
        ZX_ASSERT_MSG(FoundModule(*info), "This module was not found by __sanitizer_module_loaded");
        return 0;
      },
      &num_found_mods);
  ZX_ASSERT_MSG(num_found_mods == gNumLoadedMods, "Not all modules were accounted for");
}

// Get the index of a module in `gPhdrInfo`, assuming it does exist.
size_t ModuleIndex(std::string_view mod_name) {
  for (size_t i = 0; i < gNumLoadedMods; ++i) {
    const dl_phdr_info& found_info = gPhdrInfo[i];
    if (mod_name == found_info.dlpi_name)
      return i;
  }
  __UNREACHABLE;
}

}  // namespace

__EXPORT void __sanitizer_module_loaded(const struct dl_phdr_info* info, size_t size) {
  // Record all found modules.
  ZX_ASSERT_MSG(gNumLoadedMods < kBuffSize, "Found more than expected number of loaded modules");
  gPhdrInfo[gNumLoadedMods++] = *info;
  ++gNumNewMods;
  gOperation = kRanModuleLoaded;
}

__EXPORT void __sanitizer_startup_hook(int argc, char** argv, char** envp, void* stack_base,
                                       size_t stack_size) {
  ZX_ASSERT_MSG(gOperation == kRanModuleLoaded,
                "__sanitizer_module_loaded did not run before __sanitizer_startup_hook");
  gOperation = kRanStartupHook;
}

__attribute__((constructor)) static void ModuleConstructor() {
  ZX_ASSERT_MSG(gOperation == kRanStartupHook,
                "__sanitizer_startup_hook did not run before module constructors");
  gOperation = kRanModuleCtors;
}

int main() {
  ZX_ASSERT_MSG(gOperation == kRanModuleCtors, "Module constructors did not run before main");

  // Assert all loaded modules were found via __sanitizer_module_loaded.
  VerifyLoadedModules();

  // Assert the callback saw dependencies of this main executable before it saw
  // the main executable itself.
  size_t needed_dso_idx = ModuleIndex("libsanitizer-module-loaded-test-needed-dso.so");
  size_t exe_idx = ModuleIndex("");  // The main executable has no name.
  ZX_ASSERT_MSG(needed_dso_idx < exe_idx, "Did not see needed DSO before main executable");
  ZX_ASSERT_MSG(exe_idx == gNumLoadedMods - 1, "Expected the main executable to be last");

  // Now load a new library with its own dependency and assert that the the hook is called before
  // any of those libs' module constructors are called.
  gNumNewMods = 0;
  gOperation = kNothing;
  void* handle = dlopen(kDlopenDso, RTLD_GLOBAL);
  if (!handle)
    return -1;

  ZX_ASSERT_MSG(
      gNumNewMods == kExpectedNumDlopenMods,
      "Expected only libsanitizer-module-loaded-test-dlopen-dso.so and libsanitizer-module-loaded-test-needed-dso.so to be loaded");
  VerifyLoadedModules();

  size_t needed_dlopen_dso_idx =
      ModuleIndex("libsanitizer-module-loaded-test-dlopen-needed-dso.so");
  size_t dlopen_dso_idx = ModuleIndex("libsanitizer-module-loaded-test-dlopen-dso.so");
  ZX_ASSERT_MSG(needed_dlopen_dso_idx < dlopen_dso_idx,
                "Did not see dso needed by dlopen'd dso before the dlopen'd dso");
  ZX_ASSERT_MSG(dlopen_dso_idx == gNumLoadedMods - 1, "Expected the new dlopen'd dso to be last");

  dlclose(handle);

  return 0;
}
