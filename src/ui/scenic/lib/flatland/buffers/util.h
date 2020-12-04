// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_BUFFERS_UTIL_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_BUFFERS_UTIL_H_

#include "src/ui/scenic/lib/flatland/buffers/buffer_collection.h"

namespace flatland {

extern const fuchsia::sysmem::BufferUsage kNoneUsage;

// TODO(fxbug.dev/55193): The default memory constraints set by Sysmem only allows using
// CPU domain for buffers with CPU usage, while Mali driver asks for only
// RAM and Inaccessible domains for buffer allocation, which caused failure in
// sysmem allocation. So here we add RAM domain support to clients in order
// to get buffer allocated correctly.
const std::pair<fuchsia::sysmem::BufferUsage, fuchsia::sysmem::BufferMemoryConstraints>
GetUsageAndMemoryConstraintsForCpuWriteOften();

// Sets the client constraints on a sysmem buffer collection, including the number of images,
// the dimensionality (width, height) of those images, the usage and memory constraints. This
// is a blocking function that will wait until the constraints have been fully set.
void SetClientConstraintsAndWaitForAllocated(
    fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
    fuchsia::sysmem::BufferCollectionTokenSyncPtr token, uint32_t image_count = 1,
    uint32_t width = 64, uint32_t height = 32, fuchsia::sysmem::BufferUsage usage = kNoneUsage,
    std::optional<fuchsia::sysmem::BufferMemoryConstraints> memory_constraints = std::nullopt);

// Sets the constraints on a client buffer collection pointer and returns that pointer back to
// the caller, *without* waiting for the constraint setting to finish. It is up to the caller
// to wait until constraints are set.
fuchsia::sysmem::BufferCollectionSyncPtr CreateClientPointerWithConstraints(
    fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
    fuchsia::sysmem::BufferCollectionTokenSyncPtr token, uint32_t image_count = 1,
    uint32_t width = 64, uint32_t height = 32, fuchsia::sysmem::BufferUsage usage = kNoneUsage,
    std::optional<fuchsia::sysmem::BufferMemoryConstraints> memory_constraints = std::nullopt);

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_BUFFERS_UTIL_H_
