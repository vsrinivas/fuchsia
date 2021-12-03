// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_BIN_OPENCL_LOADER_MAGMA_DEPENDENCY_INJECTION_H_
#define SRC_GRAPHICS_BIN_OPENCL_LOADER_MAGMA_DEPENDENCY_INJECTION_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/cpp/component_context.h>

#include "src/lib/fsl/io/device_watcher.h"

class MagmaDependencyInjection {
 public:
  explicit MagmaDependencyInjection(sys::ComponentContext* context) : context_(context) {}
  zx_status_t Initialize();

 private:
  sys::ComponentContext* context_;
  std::unique_ptr<fsl::DeviceWatcher> gpu_dependency_injection_watcher_;
};

#endif  // SRC_GRAPHICS_BIN_OPENCL_LOADER_MAGMA_DEPENDENCY_INJECTION_H_
