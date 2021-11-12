// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_BIN_VULKAN_LOADER_MAGMA_DEVICE_H_
#define SRC_GRAPHICS_BIN_VULKAN_LOADER_MAGMA_DEVICE_H_

#include <fuchsia/gpu/magma/cpp/fidl.h>
#include <lib/fit/thread_checker.h>
#include <lib/sys/inspect/cpp/component.h>

#include <string>

#include "src/graphics/bin/vulkan_loader/gpu_device.h"
#include "src/graphics/bin/vulkan_loader/icd_list.h"
#include "src/lib/fxl/macros.h"

class LoaderApp;

class MagmaDevice : public GpuDevice {
 public:
  static std::unique_ptr<MagmaDevice> Create(LoaderApp* app, int dir_fd, std::string name,
                                             inspect::Node* parent);

  IcdList& icd_list() override { return icd_list_; }

 private:
  explicit MagmaDevice(LoaderApp* app) : GpuDevice(app) {}

  bool Initialize(int dir_fd, std::string name, inspect::Node* parent);

  FIT_DECLARE_THREAD_CHECKER(main_thread_)
  IcdList icd_list_;

  fuchsia::gpu::magma::DevicePtr device_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(MagmaDevice);
};

#endif  // SRC_GRAPHICS_BIN_VULKAN_LOADER_MAGMA_DEVICE_H_
