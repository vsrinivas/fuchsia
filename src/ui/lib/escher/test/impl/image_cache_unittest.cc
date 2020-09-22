// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

#include "src/ui/lib/escher/impl/image_cache.h"

#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/ui/lib/escher/test/vk/fake_gpu_allocator.h"
#include "src/ui/lib/escher/util/image_utils.h"

namespace escher {
namespace impl {
namespace {

TEST(ImageCache, SmokeTest) {
  test::FakeGpuAllocator allocator;
  ImageCache cache(EscherWeakPtr(), &allocator);

  static const vk::Format kFormat = vk::Format::eR8G8B8A8Unorm;
  static const vk::ImageUsageFlags kUsage =
      vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;

  static const int kWidth = 16;
  static const int kHeight = 16;
  static const size_t kMemorySize = kWidth * kHeight * image_utils::BytesPerPixel(kFormat);

  ImageInfo info;
  info.format = kFormat;
  info.width = kWidth;
  info.height = kHeight;
  info.usage = kUsage;

  EXPECT_EQ(0u, allocator.GetTotalBytesAllocated());

  // TODO(fxbug.dev/23725): ImageCache holds onto every image allocated. So we only need to
  // test for the high memory watermark.
  auto image0 = cache.NewImage(info);
  EXPECT_EQ(kMemorySize, allocator.GetTotalBytesAllocated());
  image0 = nullptr;
  EXPECT_EQ(kMemorySize, allocator.GetTotalBytesAllocated());
  image0 = cache.NewImage(info);
  EXPECT_EQ(kMemorySize, allocator.GetTotalBytesAllocated());
  auto image1 = cache.NewImage(info);
  EXPECT_EQ(2 * kMemorySize, allocator.GetTotalBytesAllocated());

  // Release all images.
  image0 = nullptr;
  image1 = nullptr;

  static const int kBigWidth = 1024;
  static const int kBigHeight = 1024;
  static const size_t kBigMemorySize = kBigWidth * kBigHeight * image_utils::BytesPerPixel(kFormat);

  ImageInfo big_info;
  big_info.format = kFormat;
  big_info.width = kBigWidth;
  big_info.height = kBigHeight;
  big_info.usage = kUsage;

  // Allocating an image with different parameters results in a new allocation.
  // All old memory is still allocated.
  auto big_image0 = cache.NewImage(big_info);
  EXPECT_EQ(kBigMemorySize + 2 * kMemorySize, allocator.GetTotalBytesAllocated());

  // Requesting the old image info results in memory being reused.
  image0 = cache.NewImage(info);
  EXPECT_EQ(kBigMemorySize + 2 * kMemorySize, allocator.GetTotalBytesAllocated());
  image1 = cache.NewImage(info);
  EXPECT_EQ(kBigMemorySize + 2 * kMemorySize, allocator.GetTotalBytesAllocated());
}

}  // namespace
}  // namespace impl
}  // namespace escher
