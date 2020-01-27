// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SYSTEM_MONITOR_HARVESTER_GATHER_INTROSPECTION_H_
#define GARNET_BIN_SYSTEM_MONITOR_HARVESTER_GATHER_INTROSPECTION_H_

#include "gather_category.h"

namespace harvester {

// Gather inspect information for components.
class GatherIntrospection : public GatherCategory {
 public:
  GatherIntrospection(zx_handle_t root_resource,
                      harvester::DockyardProxy* dockyard_proxy)
      : GatherCategory(root_resource, dockyard_proxy) {}

  // GatherCategory.
  void Gather() override;
};

}  // namespace harvester

#endif  // GARNET_BIN_SYSTEM_MONITOR_HARVESTER_GATHER_INTROSPECTION_H_
