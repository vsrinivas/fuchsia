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

TEST(CreateContiguousBufferCollectionInfo, CreatesCollection) {
  zx_handle_t bti_handle;
  ASSERT_EQ(fake_bti_create(&bti_handle), ZX_OK);

  fuchsia_sysmem_BufferCollectionInfo buffer_collection;
  ASSERT_EQ(CreateContiguousBufferCollectionInfo(&buffer_collection, bti_handle, kWidth, kHeight,
                                                 kNumberOfBuffers),
            ZX_OK);

  // Check it made the buffer collection like we want:
  EXPECT_EQ(buffer_collection.buffer_count, kNumberOfBuffers);
  EXPECT_EQ(buffer_collection.format.image.width, kWidth);
  EXPECT_EQ(buffer_collection.format.image.height, kHeight);
  for (uint32_t i = 0; i < countof(buffer_collection.vmos); ++i) {
    if (i < kNumberOfBuffers) {
      EXPECT_FALSE(buffer_collection.vmos[i] == ZX_HANDLE_INVALID);
    } else {
      EXPECT_TRUE(buffer_collection.vmos[i] == ZX_HANDLE_INVALID);
    }
  }
}

TEST(CreateContiguousBufferCollectionInfo, FailsOnBadHandle) {
  zx_handle_t bti_handle = ZX_HANDLE_INVALID;
  fuchsia_sysmem_BufferCollectionInfo buffer_collection;
  ASSERT_DEATH(camera::CreateContiguousBufferCollectionInfo(&buffer_collection, bti_handle, kWidth,
                                                            kHeight, kNumberOfBuffers),
               "fake bti_pin: Bad handle 0");
}

}  // namespace
}  // namespace camera
