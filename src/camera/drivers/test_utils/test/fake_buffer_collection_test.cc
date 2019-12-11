// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sysmem/c/fidl.h>
#include <lib/fake-bti/bti.h>
#include <lib/zx/vmo.h>
#include <stdlib.h>
#include <unistd.h>

#include <ddk/debug.h>
#include <gtest/gtest.h>

#include "../fake-buffer-collection.h"

namespace camera {
namespace {

constexpr uint32_t kWidth = 1080;
constexpr uint32_t kHeight = 764;
constexpr uint32_t kNumberOfBuffers = 8;

TEST(CreateContiguousBufferCollectionInfo2, CreatesCollection2) {
  zx_handle_t bti_handle;
  ASSERT_EQ(fake_bti_create(&bti_handle), ZX_OK);

  fuchsia_sysmem_BufferCollectionInfo_2 buffer_collection;
  fuchsia_sysmem_ImageFormat_2 image_format;

  EXPECT_EQ(GetImageFormat(image_format, fuchsia_sysmem_PixelFormatType_NV12, kWidth, kHeight),
            ZX_OK);
  ASSERT_EQ(CreateContiguousBufferCollectionInfo(buffer_collection, image_format, bti_handle,
                                                 kNumberOfBuffers),
            ZX_OK);

  // Check it made the buffer collection like we want:
  EXPECT_EQ(buffer_collection.buffer_count, kNumberOfBuffers);
  EXPECT_EQ(image_format.coded_width, kWidth);
  EXPECT_EQ(image_format.coded_height, kHeight);
  for (uint32_t i = 0; i < countof(buffer_collection.buffers); ++i) {
    if (i < kNumberOfBuffers) {
      EXPECT_FALSE(buffer_collection.buffers[i].vmo == ZX_HANDLE_INVALID);
    } else {
      EXPECT_TRUE(buffer_collection.buffers[i].vmo == ZX_HANDLE_INVALID);
    }
  }

  // Clean up
  EXPECT_EQ(DestroyContiguousBufferCollection(buffer_collection), ZX_OK);
  fake_bti_destroy(bti_handle);
}

TEST(CreateContiguousBufferCollectionInfo2, FailsOnBadHandle) {
  zx_handle_t bti_handle = ZX_HANDLE_INVALID;
  fuchsia_sysmem_BufferCollectionInfo_2 buffer_collection;
  fuchsia_sysmem_ImageFormat_2 image_format;

  EXPECT_EQ(GetImageFormat(image_format, fuchsia_sysmem_PixelFormatType_NV12, kWidth, kHeight),
            ZX_OK);
  EXPECT_EQ(camera::CreateContiguousBufferCollectionInfo(buffer_collection, image_format,
                                                         bti_handle, kNumberOfBuffers),
            ZX_ERR_INVALID_ARGS);

  // Cleanup
  EXPECT_EQ(DestroyContiguousBufferCollection(buffer_collection), ZX_OK);
}
}  // namespace
}  // namespace camera
