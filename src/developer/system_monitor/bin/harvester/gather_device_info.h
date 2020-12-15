// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_GATHER_DEVICE_INFO_H_
#define SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_GATHER_DEVICE_INFO_H_

#include "build_info.h"
#include "gather_category.h"

namespace harvester {

extern const char kAnnotationBuildBoard[];
extern const char kAnnotationBuildProduct[];
extern const char kAnnotationDeviceBoardName[];
extern const char kAnnotationUptime[];

// Collect static information about the current device.
class GatherDeviceInfo : public GatherCategory {
 public:
  GatherDeviceInfo(zx_handle_t root_resource,
                   harvester::DockyardProxy* dockyard_proxy)
      : GatherDeviceInfo(root_resource, dockyard_proxy,
                         std::make_unique<AnnotationsProvider>()) {}

  GatherDeviceInfo(zx_handle_t root_resource,
                   harvester::DockyardProxy* dockyard_proxy,
                   std::unique_ptr<AnnotationsProvider> annotations_provider)
      : GatherCategory(root_resource, dockyard_proxy),
        annotations_provider_(std::move(annotations_provider)) {}

  // GatherCategory.
  void Gather() override;
  void GatherDeviceProperties() override;

 private:
  std::unique_ptr<AnnotationsProvider> annotations_provider_;
};

}  // namespace harvester

#endif  // SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_GATHER_DEVICE_INFO_H_
