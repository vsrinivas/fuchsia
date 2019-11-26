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
  const size_t image_size = sizeof(uint8_t) * 4;
  auto writer = uploader1->AcquireWriter(image_size);

  auto uploader2 = BatchGpuUploader::New(escher);
  auto writer2 = uploader2->AcquireWriter(image_size);

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
  void* host_ptr = writer->host_ptr();
  uint8_t const color_1[] = {100, 90, 80, 255};
  memcpy(host_ptr, color_1, sizeof(color_1));
  // We set image layout to eTransferDstOptimal as we need to write to it
  // later.
  writer->WriteImage(image, region, vk::ImageLayout::eTransferDstOptimal);
  // Posting and submitting should succeed.
  uploader1->PostWriter(std::move(writer));

  void* host_ptr2 = writer2->host_ptr();
  uint8_t const color_2[] = {200, 190, 180, 255};
  memcpy(host_ptr2, color_2, sizeof(color_2));
  writer2->WriteImage(image, region);
  // Posting and submitting should succeed.
  uploader2->PostWriter(std::move(writer2));

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
  downloader->ScheduleReadImage(
      image, region, [&color_2, &downloaded, &image_correct](const void* host_ptr, size_t size) {
        downloaded = true;
        image_correct = memcmp(host_ptr, color_2, image_size) == 0;
      });
  downloader->Submit();
  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(downloaded && image_correct);
}

}  // namespace escher
