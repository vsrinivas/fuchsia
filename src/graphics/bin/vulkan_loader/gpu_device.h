// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_BIN_VULKAN_LOADER_GPU_DEVICE_H_
#define SRC_GRAPHICS_BIN_VULKAN_LOADER_GPU_DEVICE_H_

#include <lib/sys/inspect/cpp/component.h>

#include <cstdint>
#include <vector>

class IcdList;
class LoaderApp;

class GpuDevice {
 public:
  virtual ~GpuDevice() = default;

  virtual IcdList& icd_list() = 0;

  uint64_t icd_count() { return icds_.size(); }

 protected:
  struct IcdData {
    inspect::Node node;
    inspect::ValueList values;
  };

  explicit GpuDevice(LoaderApp* app) : app_(app) {}

  LoaderApp* app() { return app_; }
  inspect::Node& node() { return node_; }
  std::vector<IcdData>& icds() { return icds_; }

 private:
  LoaderApp* app_;
  inspect::Node node_;
  std::vector<IcdData> icds_;
};

#endif  // SRC_GRAPHICS_BIN_VULKAN_LOADER_GPU_DEVICE_H_
