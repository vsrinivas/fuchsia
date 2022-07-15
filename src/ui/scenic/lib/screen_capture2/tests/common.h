// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCREEN_CAPTURE2_TESTS_COMMON_H_
#define SRC_UI_SCENIC_LIB_SCREEN_CAPTURE2_TESTS_COMMON_H_

#include <fuchsia/sysmem/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/zx/event.h>

#include "src/ui/scenic/lib/allocation/allocator.h"
#include "src/ui/scenic/lib/allocation/buffer_collection_import_export_tokens.h"
#include "src/ui/scenic/lib/screen_capture/screen_capture_buffer_collection_importer.h"

namespace screen_capture2 {
namespace test {

std::shared_ptr<allocation::Allocator> CreateAllocator(
    std::shared_ptr<screen_capture::ScreenCaptureBufferCollectionImporter> importer,
    sys::ComponentContext* app_context);

void CreateBufferCollectionInfo2WithConstraints(
    fuchsia::sysmem::BufferCollectionConstraints constraints,
    allocation::BufferCollectionExportToken export_token,
    std::shared_ptr<allocation::Allocator> flatland_allocator,
    fuchsia::sysmem::Allocator_Sync* sysmem_allocator);

}  // namespace test
}  // namespace screen_capture2
#endif  // SRC_UI_SCENIC_LIB_SCREEN_CAPTURE2_TESTS_COMMON_H_
