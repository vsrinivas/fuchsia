// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/bin/vulkan_loader/app.h"

#include "src/graphics/bin/vulkan_loader/goldfish_device.h"
#include "src/graphics/bin/vulkan_loader/magma_device.h"

zx_status_t LoaderApp::InitDeviceWatcher() {
  gpu_watcher_ =
      fsl::DeviceWatcher::Create("/dev/class/gpu", [this](int dir_fd, const std::string filename) {
        auto device = MagmaDevice::Create(this, dir_fd, filename, &devices_node_);
        if (device) {
          devices_.emplace_back(std::move(device));
        }
      });
  if (!gpu_watcher_)
    return ZX_ERR_INTERNAL;
  goldfish_watcher_ = fsl::DeviceWatcher::Create(
      "/dev/class/goldfish-pipe", [this](int dir_fd, const std::string filename) {
        auto device = GoldfishDevice::Create(this, dir_fd, filename, &devices_node_);
        if (device) {
          devices_.emplace_back(std::move(device));
        }
      });
  if (!goldfish_watcher_)
    return ZX_ERR_INTERNAL;
  return ZX_OK;
}

void LoaderApp::RemoveDevice(GpuDevice* device) {
  auto it = std::remove_if(devices_.begin(), devices_.end(),
                           [device](const std::unique_ptr<GpuDevice>& unique_device) {
                             return device == unique_device.get();
                           });
  devices_.erase(it, devices_.end());
}
