// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_INFO_RESOURCE_H_
#define SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_INFO_RESOURCE_H_

#include <zircon/status.h>
#include <zircon/types.h>

namespace harvester {

// Get a handle to the info resource, which can be used to find its children
// and so on to review a tree of resources.
zx_status_t GetInfoResource(zx_handle_t* info_resource_handle);

}  // namespace harvester

#endif  // SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_INFO_RESOURCE_H_
