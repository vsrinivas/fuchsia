// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/examples/screen_recording/screen_capture_helper.h"

#include "src/ui/scenic/lib/flatland/buffers/util.h"
#include "src/ui/scenic/lib/utils/helpers.h"
#include "zircon/system/ulib/fbl/include/fbl/algorithm.h"

namespace screen_recording_example {

using flatland::MapHostPointer;
using fuchsia::ui::composition::RegisterBufferCollectionArgs;
using fuchsia::ui::composition::RegisterBufferCollectionUsages;

fuchsia::sysmem::BufferCollectionInfo_2 CreateBufferCollectionInfo2WithConstraints(
    fuchsia::sysmem::BufferCollectionConstraints constraints,
    allocation::BufferCollectionExportToken export_token,
    fuchsia::ui::composition::Allocator_Sync* flatland_allocator,
    fuchsia::sysmem::Allocator_Sync* sysmem_allocator, RegisterBufferCollectionUsages usage) {
  FX_DCHECK(flatland_allocator);
  FX_DCHECK(sysmem_allocator);

  RegisterBufferCollectionArgs rbc_args = {};

  zx_status_t status;
  // Create Sysmem tokens.
  auto [local_token, dup_token] = utils::CreateSysmemTokens(sysmem_allocator);

  rbc_args.set_export_token(std::move(export_token));
  rbc_args.set_buffer_collection_token(std::move(dup_token));
  rbc_args.set_usages(usage);

  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
  status = sysmem_allocator->BindSharedCollection(std::move(local_token),
                                                  buffer_collection.NewRequest());
  FX_DCHECK(status == ZX_OK);

  status = buffer_collection->SetConstraints(true, constraints);
  FX_DCHECK(status == ZX_OK);

  fuchsia::ui::composition::Allocator_RegisterBufferCollection_Result result;
  flatland_allocator->RegisterBufferCollection(std::move(rbc_args), &result);
  FX_DCHECK(!result.is_err());

  // Wait for allocation.
  zx_status_t allocation_status = ZX_OK;
  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info = {};
  status = buffer_collection->WaitForBuffersAllocated(&allocation_status, &buffer_collection_info);
  FX_DCHECK(ZX_OK == status);
  FX_DCHECK(ZX_OK == allocation_status);
  FX_DCHECK(constraints.min_buffer_count == buffer_collection_info.buffer_count);

  buffer_collection->Close();
  return buffer_collection_info;
}

}  // namespace screen_recording_example
