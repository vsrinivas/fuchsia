// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_UTILS_H_
#define SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_UTILS_H_

#include <fidl/fuchsia.sysmem2/cpp/wire.h>

bool IsWriteUsage(const fuchsia_sysmem2::wire::BufferUsage& buffer_usage);

bool IsCpuUsage(const fuchsia_sysmem2::wire::BufferUsage& buffer_usage);

bool IsAnyUsage(const fuchsia_sysmem2::wire::BufferUsage& buffer_usage);

#endif  // SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_UTILS_H_
