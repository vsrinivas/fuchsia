// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_TESTS_UTILS_SCREEN_CAPTURE_UTILS_H_
#define SRC_UI_SCENIC_TESTS_UTILS_SCREEN_CAPTURE_UTILS_H_

#include <fuchsia/ui/composition/cpp/fidl.h>

#include "src/ui/scenic/lib/allocation/allocator.h"
#include "src/ui/scenic/lib/allocation/buffer_collection_import_export_tokens.h"
#include "src/ui/scenic/lib/utils/helpers.h"

namespace integration_tests {

using fuchsia::math::SizeU;
using fuchsia::math::Vec;
using fuchsia::ui::composition::ChildViewWatcher;
using fuchsia::ui::composition::ContentId;
using fuchsia::ui::composition::ParentViewportWatcher;
using fuchsia::ui::composition::RegisterBufferCollectionUsage;
using fuchsia::ui::composition::TransformId;
using fuchsia::ui::composition::ViewportProperties;

static constexpr uint32_t kBytesPerPixel = 4;

// BGRA
static constexpr uint8_t kRed[] = {0, 0, 255, 255};
static constexpr uint8_t kGreen[] = {0, 255, 0, 255};
static constexpr uint8_t kBlue[] = {255, 0, 0, 255};
static constexpr uint8_t kYellow[] = {0, 255, 255, 255};

bool PixelEquals(const uint8_t* a, const uint8_t* b);

void AppendPixel(std::vector<uint8_t>* values, const uint8_t* pixel);

void GenerateImageForFlatlandInstance(uint32_t buffer_collection_index,
                                      fuchsia::ui::composition::FlatlandPtr& flatland,
                                      TransformId parent_transform,
                                      allocation::BufferCollectionImportToken import_token,
                                      SizeU size, Vec translation, uint32_t image_id,
                                      uint32_t transform_id);

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
    fuchsia::sysmem::Allocator_Sync* sysmem_allocator, RegisterBufferCollectionUsage usage);

// This function returns a linear buffer of pixels of size width * height.
std::vector<uint8_t> ExtractScreenCapture(
    uint32_t buffer_id, fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection_info,
    uint32_t kBytesPerPixel, uint32_t render_target_width, uint32_t render_target_height);

}  // namespace integration_tests

#endif  // SRC_UI_SCENIC_TESTS_UTILS_SCREEN_CAPTURE_UTILS_H_
