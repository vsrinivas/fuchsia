// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/weave/lib/applets_loader/testing/test_applets.h"

#include <dlfcn.h>

#include "src/connectivity/weave/applets/test_applets/test_applets.h"

namespace weavestack::applets::testing {

std::shared_ptr<TestAppletsModuleExt> OpenTestAppletsExt() {
  void* lib = dlopen(kTestAppletsModuleName, RTLD_GLOBAL | RTLD_LAZY);
  if (!lib) {
    return nullptr;
  }
  void* sym = dlsym(lib, "TestAppletsModuleExt_instance");
  if (!sym) {
    FX_LOGS(ERROR) << "Failed to open test applet: " << dlerror();
    return nullptr;
  }
  // Since the pointer is owned by the loaded module; we don't free() the pointer but instead just
  // dlclose the shared lib that owns the pointer.
  return std::shared_ptr<TestAppletsModuleExt>(reinterpret_cast<TestAppletsModuleExt*>(sym),
                                               [lib](auto* ptr) { dlclose(lib); });
}

}  // namespace weavestack::applets::testing
