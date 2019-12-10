// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/chained_semaphore_generator.h"

#include "src/ui/lib/escher/renderer/batch_gpu_downloader.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/test/gtest_escher.h"
#include "src/ui/lib/escher/util/image_utils.h"

namespace escher {

using ChainedSemaphoreGeneratorTest = escher::test::TestWithVkValidationLayer;

// We upload to an image twice using different pixel information, and we use
// ChainedSemaphoreGenerator to ensure that the pixel finally uploaded to
// the image is the latter one we uploaded.
VK_TEST_F(ChainedSemaphoreGeneratorTest, SequentialUpload) {
  auto escher = test::GetEscher()->GetWeakPtr();
  auto uploader1 = BatchGpuUploader::New(escher);
  auto uploader2 = BatchGpuUploader::New(escher);
  const size_t image_size = sizeof(uint8_t) * 4;

  // Create a 1x1 RGBA (8-bit channels) image to write to.
  ImageFactoryAdapter image_factory(escher->gpu_allocator(), escher->resource_recycler());
  ImagePtr image = image_utils::NewImage(&image_factory, vk::Format::eR8G8B8A8Unorm, 1, 1);
  vk::BufferImageCopy region;
  region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageExtent.width = 1;
  region.imageExtent.height = 1;
  region.imageExtent.depth = 1;
  region.bufferOffset = 0;

  // Do write.
  std::vector<uint8_t> host_data = {100, 90, 80, 255};
  // We set image layout to eTransferDstOptimal as we need to write to it
  // later.
  uploader1->ScheduleWriteImage(image, std::move(host_data), vk::ImageLayout::eTransferDstOptimal,
                                region);

  std::vector<uint8_t> host_data_2 = {200, 190, 180, 255};
  uploader2->ScheduleWriteImage(image, std::move(host_data_2));

  // Set up semaphore chain.
  auto* semaphore_chain = escher->semaphore_chain();
  auto semaphore_pair_writer1 = semaphore_chain->TakeLastAndCreateNextSemaphore();
  uploader1->AddSignalSemaphore(semaphore_pair_writer1.semaphore_to_signal);

  auto semaphore_pair_writer2 = semaphore_chain->TakeLastAndCreateNextSemaphore();
  uploader2->AddWaitSemaphore(semaphore_pair_writer2.semaphore_to_wait,
                              vk::PipelineStageFlagBits::eTransfer);

  EXPECT_EQ(semaphore_pair_writer1.semaphore_to_signal->vk_semaphore(),
            semaphore_pair_writer2.semaphore_to_wait->vk_semaphore());

  // Submit the work.
  uploader1->Submit();
  uploader2->Submit();
  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());

  // Check if the image downloaded contains color_2.
  auto downloader = BatchGpuDownloader::New(escher);
  bool downloaded = false;
  bool image_correct = false;
  std::vector<uint8_t> color_2 = {200, 190, 180, 255};
  downloader->ScheduleReadImage(
      image, region, [&color_2, &downloaded, &image_correct](const void* host_ptr, size_t size) {
        downloaded = true;
        image_correct = memcmp(host_ptr, color_2.data(), image_size) == 0;
      });
  downloader->Submit();
  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(downloaded && image_correct);
}

}  // namespace escher
