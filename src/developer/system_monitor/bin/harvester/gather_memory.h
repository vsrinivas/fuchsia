// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_GATHER_MEMORY_H_
#define SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_GATHER_MEMORY_H_

#include "gather_category.h"

namespace harvester {

class SampleBundle;

void AddGlobalMemorySamples(SampleBundle* samples, zx_handle_t info_resource);

// Gather high level memory information from the kernel.
class GatherMemory : public GatherCategory {
 public:
  GatherMemory(zx_handle_t info_resource,
               harvester::DockyardProxy* dockyard_proxy)
      : GatherCategory(info_resource, dockyard_proxy) {}

  // GatherCategory.
  void GatherDeviceProperties() override;
  void Gather() override;
};

}  // namespace harvester

#endif  // SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_GATHER_MEMORY_H_
