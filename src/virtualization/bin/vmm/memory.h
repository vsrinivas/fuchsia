// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_MEMORY_H_
#define SRC_VIRTUALIZATION_BIN_VMM_MEMORY_H_

#include <fuchsia/virtualization/cpp/fidl.h>
#include <zircon/boot/image.h>

#include <vector>

#include "src/virtualization/bin/vmm/dev_mem.h"

// Constructs an array of zbi_mem_range_t ranges describing the physical
// memory layout.
std::vector<zbi_mem_range_t> ZbiMemoryRanges(
    const std::vector<fuchsia::virtualization::MemorySpec>& specs, size_t mem_size,
    const DevMem& dev_mem);

#endif  // SRC_VIRTUALIZATION_BIN_VMM_MEMORY_H_
