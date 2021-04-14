// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_BIN_VULKAN_LOADER_APP_H_
#define SRC_GRAPHICS_BIN_VULKAN_LOADER_APP_H_

#include <lib/sys/cpp/component_context.h>
#include <lib/sys/inspect/cpp/component.h>

#include "src/graphics/bin/vulkan_loader/gpu_device.h"
#include "src/lib/fsl/io/device_watcher.h"

class MagmaDevice;

class LoaderApp {
 public:
  explicit LoaderApp(sys::ComponentContext* context) : inspector_(context) {
    devices_node_ = inspector_.root().CreateChild("devices");
  }

  zx_status_t InitDeviceWatcher();

  void AddDevice(std::unique_ptr<GpuDevice> device) { devices_.push_back(std::move(device)); }
  void RemoveDevice(GpuDevice* device);

  size_t device_count() const { return devices_.size(); }

 private:
  sys::ComponentInspector inspector_;
  inspect::Node devices_node_;
  std::unique_ptr<fsl::DeviceWatcher> gpu_watcher_;
  std::unique_ptr<fsl::DeviceWatcher> goldfish_watcher_;

  std::vector<std::unique_ptr<GpuDevice>> devices_;
};

#endif  // SRC_GRAPHICS_BIN_VULKAN_LOADER_APP_H_
