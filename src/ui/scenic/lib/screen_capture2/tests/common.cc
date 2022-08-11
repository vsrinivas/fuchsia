// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/screen_capture2/tests/common.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/ui/scenic/lib/allocation/allocator.h"
#include "src/ui/scenic/lib/allocation/buffer_collection_import_export_tokens.h"
#include "src/ui/scenic/lib/flatland/engine/engine.h"
#include "src/ui/scenic/lib/screen_capture/screen_capture_buffer_collection_importer.h"
#include "src/ui/scenic/lib/utils/helpers.h"

using testing::_;

using allocation::Allocator;
using allocation::BufferCollectionImporter;
using fuchsia::ui::composition::RegisterBufferCollectionArgs;
using fuchsia::ui::composition::RegisterBufferCollectionUsages;
using screen_capture::ScreenCaptureBufferCollectionImporter;

namespace screen_capture2 {
namespace test {

std::shared_ptr<Allocator> CreateAllocator(
    std::shared_ptr<screen_capture::ScreenCaptureBufferCollectionImporter> importer,
    sys::ComponentContext* app_context) {
  std::vector<std::shared_ptr<BufferCollectionImporter>> extra_importers;
  std::vector<std::shared_ptr<BufferCollectionImporter>> screenshot_importers;
  screenshot_importers.push_back(importer);
  return std::make_shared<Allocator>(app_context, extra_importers, screenshot_importers,
                                     utils::CreateSysmemAllocatorSyncPtr("-allocator"));
}

void CreateBufferCollectionInfo2WithConstraints(
    fuchsia::sysmem::BufferCollectionConstraints constraints,
    allocation::BufferCollectionExportToken export_token,
    std::shared_ptr<Allocator> flatland_allocator,
    fuchsia::sysmem::Allocator_Sync* sysmem_allocator) {
  RegisterBufferCollectionArgs rbc_args = {};

  zx_status_t status;
  // Create Sysmem tokens.
  auto [local_token, dup_token] = utils::CreateSysmemTokens(sysmem_allocator);

  rbc_args.set_export_token(std::move(export_token));
  rbc_args.set_buffer_collection_token(std::move(dup_token));
  rbc_args.set_usages(RegisterBufferCollectionUsages::SCREENSHOT);

  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
  status = sysmem_allocator->BindSharedCollection(std::move(local_token),
                                                  buffer_collection.NewRequest());
  FX_DCHECK(status == ZX_OK);

  status = buffer_collection->SetConstraints(true, constraints);
  EXPECT_EQ(status, ZX_OK);

  bool processed_callback = false;
  flatland_allocator->RegisterBufferCollection(
      std::move(rbc_args),
      [&processed_callback](
          fuchsia::ui::composition::Allocator_RegisterBufferCollection_Result result) {
        EXPECT_EQ(false, result.is_err());
        processed_callback = true;
      });

  // Wait for allocation.
  zx_status_t allocation_status = ZX_OK;
  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info = {};
  status = buffer_collection->WaitForBuffersAllocated(&allocation_status, &buffer_collection_info);
  ASSERT_EQ(ZX_OK, status);
  ASSERT_EQ(ZX_OK, allocation_status);
  ASSERT_EQ(constraints.min_buffer_count, buffer_collection_info.buffer_count);

  buffer_collection->Close();
}

}  // namespace test
}  // namespace screen_capture2
