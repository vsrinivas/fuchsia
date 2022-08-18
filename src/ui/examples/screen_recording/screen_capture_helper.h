// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_EXAMPLES_SCREEN_RECORDING_SCREEN_CAPTURE_HELPER_H_
#define SRC_UI_EXAMPLES_SCREEN_RECORDING_SCREEN_CAPTURE_HELPER_H_

#include <fuchsia/ui/composition/cpp/fidl.h>

#include "src/ui/scenic/lib/allocation/allocator.h"
#include "src/ui/scenic/lib/allocation/buffer_collection_import_export_tokens.h"

namespace screen_recording_example {

using fuchsia::math::SizeU;
using fuchsia::math::Vec;
using fuchsia::ui::composition::ChildViewWatcher;
using fuchsia::ui::composition::ContentId;
using fuchsia::ui::composition::ParentViewportWatcher;
using fuchsia::ui::composition::RegisterBufferCollectionUsages;
using fuchsia::ui::composition::TransformId;
using fuchsia::ui::composition::ViewportProperties;

uint32_t GetPixelsPerRow(const fuchsia::sysmem::SingleBufferSettings& settings,
                         uint32_t bytes_per_pixel, uint32_t image_width);

void WriteToSysmemBuffer(const std::vector<uint8_t>& write_values,
                         fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection_info,
                         uint32_t buffer_collection_idx, uint32_t kBytesPerPixel,
                         uint32_t image_width, uint32_t image_height);

fuchsia::sysmem::BufferCollectionInfo_2 CreateBufferCollectionInfo2WithConstraints(
    fuchsia::sysmem::BufferCollectionConstraints constraints,
    allocation::BufferCollectionExportToken export_token,
    fuchsia::ui::composition::Allocator_Sync* flatland_allocator,
    fuchsia::sysmem::Allocator_Sync* sysmem_allocator, RegisterBufferCollectionUsages usage);

// This function returns a linear buffer of pixels of size width * height.
std::vector<uint8_t> ExtractScreenCapture(
    uint32_t buffer_id, fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection_info,
    uint32_t kBytesPerPixel, uint32_t render_target_width, uint32_t render_target_height);

}  // namespace screen_recording_example

#endif  // SRC_UI_EXAMPLES_SCREEN_RECORDING_SCREEN_CAPTURE_HELPER_H_
