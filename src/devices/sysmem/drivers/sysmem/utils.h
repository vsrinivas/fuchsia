// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_UTILS_H_
#define SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_UTILS_H_

#include <fuchsia/sysmem2/llcpp/fidl.h>

bool IsWriteUsage(const llcpp::fuchsia::sysmem2::wire::BufferUsage& buffer_usage);

bool IsCpuUsage(const llcpp::fuchsia::sysmem2::wire::BufferUsage& buffer_usage);

bool IsAnyUsage(const llcpp::fuchsia::sysmem2::wire::BufferUsage& buffer_usage);

#endif  // SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_UTILS_H_
