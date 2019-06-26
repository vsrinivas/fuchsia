// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SYSTEM_MONITOR_HARVESTER_ROOT_RESOURCE_H_
#define GARNET_BIN_SYSTEM_MONITOR_HARVESTER_ROOT_RESOURCE_H_

#include <zircon/status.h>
#include <zircon/types.h>

namespace harvester {

// Get a handle to the root resource, which can be used to find its children
// and so on to review a tree of resources.
zx_status_t GetRootResource(zx_handle_t* root_resource);

}  // namespace harvester

#endif  // GARNET_BIN_SYSTEM_MONITOR_HARVESTER_ROOT_RESOURCE_H_
