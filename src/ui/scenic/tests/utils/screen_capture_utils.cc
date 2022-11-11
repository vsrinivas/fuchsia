// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/tests/utils/screen_capture_utils.h"

#include <lib/sys/component/cpp/testing/realm_builder.h>

#include <gmock/gmock.h>

#include "src/ui/scenic/lib/flatland/buffers/util.h"
#include "zircon/system/ulib/fbl/include/fbl/algorithm.h"

namespace integration_tests {
using flatland::MapHostPointer;
using RealmRoot = component_testing::RealmRoot;
using fuchsia::ui::composition::ChildViewWatcher;
using fuchsia::ui::composition::ContentId;
using fuchsia::ui::composition::ParentViewportWatcher;
using fuchsia::ui::composition::RegisterBufferCollectionArgs;
using fuchsia::ui::composition::RegisterBufferCollectionUsages;
using fuchsia::ui::composition::TransformId;
using fuchsia::ui::composition::ViewportProperties;

bool PixelEquals(const uint8_t* a, const uint8_t* b) { return memcmp(a, b, kBytesPerPixel) == 0; }

void AppendPixel(std::vector<uint8_t>* values, const uint8_t* pixel) {
  values->insert(values->end(), pixel, pixel + kBytesPerPixel);
}

void GenerateImageForFlatlandInstance(uint32_t buffer_collection_index,
                                      fuchsia::ui::composition::FlatlandPtr& flatland,
                                      TransformId parent_transform,
                                      allocation::BufferCollectionImportToken import_token,
                                      SizeU size, Vec translation, uint32_t image_id,
                                      uint32_t transform_id) {
  // Create the image in the Flatland instance.
  fuchsia::ui::composition::ImageProperties image_properties = {};
  image_properties.set_size(size);
  fuchsia::ui::composition::ContentId content_id = {.value = image_id};
  flatland->CreateImage(content_id, std::move(import_token), buffer_collection_index,
                        std::move(image_properties));

  // Add the created image as a child of the parent transform specified. Apply the right size and
  // orientation commands.
  const TransformId kTransform{.value = transform_id};
  flatland->CreateTransform(kTransform);

  flatland->SetContent(kTransform, content_id);
  flatland->SetImageDestinationSize(content_id, {size.width, size.height});
  flatland->SetTranslation(kTransform, translation);

  flatland->AddChild(parent_transform, kTransform);
}

inline uint32_t GetPixelsPerRow(const fuchsia::sysmem::SingleBufferSettings& settings,
                                uint32_t bytes_per_pixel, uint32_t image_width) {
  uint32_t bytes_per_row_divisor = settings.image_format_constraints.bytes_per_row_divisor;
  uint32_t min_bytes_per_row = settings.image_format_constraints.min_bytes_per_row;
  uint32_t bytes_per_row = fbl::round_up(std::max(image_width * bytes_per_pixel, min_bytes_per_row),
                                         bytes_per_row_divisor);
  uint32_t pixels_per_row = bytes_per_row / bytes_per_pixel;

  return pixels_per_row;
}

// This method writes to a sysmem buffer, taking into account any potential stride width
// differences. The method also flushes the cache if the buffer is in RAM domain.
void WriteToSysmemBuffer(const std::vector<uint8_t>& write_values,
                         fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection_info,
                         uint32_t buffer_collection_idx, uint32_t kBytesPerPixel,
                         uint32_t image_width, uint32_t image_height) {
  uint32_t pixels_per_row =
      GetPixelsPerRow(buffer_collection_info.settings, kBytesPerPixel, image_width);

  MapHostPointer(buffer_collection_info, buffer_collection_idx,
                 [&write_values, pixels_per_row, kBytesPerPixel, image_width, image_height](
                     uint8_t* vmo_host, uint32_t num_bytes) {
                   uint32_t bytes_per_row = pixels_per_row * kBytesPerPixel;
                   uint32_t valid_bytes_per_row = image_width * kBytesPerPixel;

                   EXPECT_GE(bytes_per_row, valid_bytes_per_row);
                   EXPECT_GE(num_bytes, bytes_per_row * image_height);

                   if (bytes_per_row == valid_bytes_per_row) {
                     // Fast path.
                     memcpy(vmo_host, write_values.data(), write_values.size());
                   } else {
                     // Copy over row-by-row.
                     for (size_t i = 0; i < image_height; ++i) {
                       memcpy(&vmo_host[i * bytes_per_row],
                              &write_values[i * image_width * kBytesPerPixel], valid_bytes_per_row);
                     }
                   }
                 });

  // Flush the cache if we are operating in RAM.
  if (buffer_collection_info.settings.buffer_settings.coherency_domain ==
      fuchsia::sysmem::CoherencyDomain::RAM) {
    EXPECT_EQ(ZX_OK, buffer_collection_info.buffers[buffer_collection_idx].vmo.op_range(
                         ZX_VMO_OP_CACHE_CLEAN, 0,
                         buffer_collection_info.settings.buffer_settings.size_bytes, nullptr, 0));
  }
}

fuchsia::sysmem::BufferCollectionInfo_2 CreateBufferCollectionInfo2WithConstraints(
    fuchsia::sysmem::BufferCollectionConstraints constraints,
    allocation::BufferCollectionExportToken export_token,
    fuchsia::ui::composition::Allocator_Sync* flatland_allocator,
    fuchsia::sysmem::Allocator_Sync* sysmem_allocator, RegisterBufferCollectionUsages usage) {
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
  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info{};
  status = buffer_collection->WaitForBuffersAllocated(&allocation_status, &buffer_collection_info);
  FX_DCHECK(ZX_OK == status);
  FX_DCHECK(ZX_OK == allocation_status);
  FX_DCHECK(constraints.min_buffer_count == buffer_collection_info.buffer_count);

  EXPECT_EQ(ZX_OK, buffer_collection->Close());
  return buffer_collection_info;
}

// This function returns a linear buffer of pixels of size width * height.
std::vector<uint8_t> ExtractScreenCapture(
    uint32_t buffer_id, fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection_info,
    uint32_t kBytesPerPixel, uint32_t render_target_width, uint32_t render_target_height) {
  // Copy ScreenCapture output for inspection. Note that the stride of the buffer may be different
  // than the width of the image, if the width of the image is not a multiple of 64.
  //
  // For instance, is the original image were 1024x600, the new width is 600. 600*4=2400 bytes,
  // which is not a multiple of 64. The next multiple would be 2432, which would mean the buffer
  // is actually a 608x1024 "pixel" buffer, since 2432/4=608. We must account for that 8 byte
  // padding when copying the bytes over to be inspected.
  EXPECT_EQ(ZX_OK, buffer_collection_info.buffers[buffer_id].vmo.op_range(
                       ZX_VMO_OP_CACHE_CLEAN_INVALIDATE, 0,
                       buffer_collection_info.settings.buffer_settings.size_bytes, nullptr, 0));

  uint32_t pixels_per_row =
      GetPixelsPerRow(buffer_collection_info.settings, kBytesPerPixel, render_target_width);
  std::vector<uint8_t> read_values;
  read_values.resize(static_cast<size_t>(render_target_width) * render_target_height *
                     kBytesPerPixel);

  MapHostPointer(buffer_collection_info, buffer_id,
                 [&read_values, kBytesPerPixel, pixels_per_row, render_target_width,
                  render_target_height](uint8_t* vmo_host, uint32_t num_bytes) {
                   uint32_t bytes_per_row = pixels_per_row * kBytesPerPixel;
                   uint32_t valid_bytes_per_row = render_target_width * kBytesPerPixel;

                   EXPECT_GE(bytes_per_row, valid_bytes_per_row);

                   if (bytes_per_row == valid_bytes_per_row) {
                     // Fast path.
                     memcpy(read_values.data(), vmo_host,
                            static_cast<size_t>(bytes_per_row) * render_target_height);
                   } else {
                     for (size_t i = 0; i < render_target_height; ++i) {
                       memcpy(&read_values[i * render_target_width * kBytesPerPixel],
                              &vmo_host[i * bytes_per_row], valid_bytes_per_row);
                     }
                   }
                 });

  return read_values;
}

}  // namespace integration_tests
