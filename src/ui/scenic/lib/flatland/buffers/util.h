// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_BUFFERS_UTIL_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_BUFFERS_UTIL_H_

#include "src/ui/scenic/lib/flatland/buffers/buffer_collection.h"

namespace flatland {

extern const fuchsia::sysmem::BufferUsage kNoneUsage;

struct SysmemTokens {
  // Token for setting client side constraints.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token;

  // Token for setting server side constraints.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr dup_token;

  static SysmemTokens Create(fuchsia::sysmem::Allocator_Sync* sysmem_allocator) {
    fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token;
    zx_status_t status = sysmem_allocator->AllocateSharedCollection(local_token.NewRequest());
    FX_DCHECK(status == ZX_OK);
    fuchsia::sysmem::BufferCollectionTokenSyncPtr dup_token;
    status = local_token->Duplicate(std::numeric_limits<uint32_t>::max(), dup_token.NewRequest());
    FX_DCHECK(status == ZX_OK);
    status = local_token->Sync();
    FX_DCHECK(status == ZX_OK);
    return {std::move(local_token), std::move(dup_token)};
  }
};

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

// Maps a sysmem vmo's bytes into host memory that can be accessed via a callback function. The
// callback provides the caller with a raw pointer to the vmo memory as well as an int for the
// number of bytes. If an out of bounds vmo_idx is provided, the callback function will call the
// user callback with mapped_ptr equal to nullptr. Once the callback function returns, the host
// pointer is unmapped and so cannot continue to be used outside of the scope of the callback.
void MapHostPointer(const fuchsia::sysmem::BufferCollectionInfo_2& collection_info,
                    uint32_t vmo_idx,
                    std::function<void(uint8_t* mapped_ptr, uint32_t num_bytes)> callback);

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_BUFFERS_UTIL_H_
