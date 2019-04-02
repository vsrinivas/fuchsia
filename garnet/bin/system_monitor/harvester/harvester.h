// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SYSTEM_MONITOR_HARVESTER_HARVESTER_H_
#define GARNET_BIN_SYSTEM_MONITOR_HARVESTER_HARVESTER_H_

#include <zircon/types.h>
#include <iostream>
#include <string>

#include "dockyard_proxy.h"
#include "garnet/lib/system_monitor/dockyard/dockyard.h"

namespace harvester {

// Gather Samples collect samples for a given subject. They are grouped to make
// the code more manageable and for enabling/disabling categories in the future.
void GatherCpuSamples(
    zx_handle_t root_resource,
    const std::unique_ptr<harvester::DockyardProxy>& dockyard_proxy);
void GatherMemorySamples(
    zx_handle_t root_resource,
    const std::unique_ptr<harvester::DockyardProxy>& dockyard_proxy);
void GatherThreadSamples(
    zx_handle_t root_resource,
    const std::unique_ptr<harvester::DockyardProxy>& dockyard_proxy);

void GatherComponentIntrospection(
    zx_handle_t root_resource,
    const std::unique_ptr<harvester::DockyardProxy>& dockyard_proxy);

}  // namespace harvester

#endif  // GARNET_BIN_SYSTEM_MONITOR_HARVESTER_HARVESTER_H_
