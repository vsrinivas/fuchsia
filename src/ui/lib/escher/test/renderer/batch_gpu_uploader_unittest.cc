// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"

#include <thread>

#include "gtest/gtest.h"
#include "src/ui/lib/escher/test/gtest_escher.h"
#include "src/ui/lib/escher/test/vk/vulkan_tester.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/vk/buffer_factory.h"
#include "src/ui/lib/escher/vk/image_factory.h"

namespace escher {

using BatchGpuUploaderTest = test::TestWithVkValidationLayer;

VK_TEST_F(BatchGpuUploaderTest, CreateDestroyUploader) {
  auto escher = test::GetEscher()->GetWeakPtr();
  bool batch_upload_done = false;

  {
    std::unique_ptr<BatchGpuUploader> uploader = BatchGpuUploader::New(escher);
    // BatchGpuUploader must be submitted before it is destroyed.
    uploader->Submit([&batch_upload_done]() { batch_upload_done = true; });
  }

  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(batch_upload_done);
}

VK_TEST_F(BatchGpuUploaderTest, InvalidUploader) {
  // A BatchGpuUploader without an escher should not be created.
  auto uploader = BatchGpuUploader::New(EscherWeakPtr());
  EXPECT_FALSE(uploader);
}

VK_TEST_F(BatchGpuUploaderTest, AcquireSubmitWriter) {
  auto escher = test::GetEscher()->GetWeakPtr();
  std::unique_ptr<BatchGpuUploader> uploader = BatchGpuUploader::New(escher);

  auto writer = uploader->AcquireWriter(256);
  uploader->PostWriter(std::move(writer));

  // BatchGpuUploader must be submitted before it is destroyed.
  bool batch_upload_done = false;
  uploader->Submit([&batch_upload_done]() { batch_upload_done = true; });
  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(batch_upload_done);
}

VK_TEST_F(BatchGpuUploaderTest, LazyInitializationTest) {
  auto escher = test::GetEscher()->GetWeakPtr();
  std::unique_ptr<BatchGpuUploader> uploader = BatchGpuUploader::New(escher);

  // BatchGpuUploader will not be initialized until instantiation of Writers.
  EXPECT_FALSE(uploader->HasContentToUpload());

  auto writer = uploader->AcquireWriter(256);
  uploader->PostWriter(std::move(writer));

  EXPECT_TRUE(uploader->HasContentToUpload());

  // BatchGpuUploader must be submitted before it is destroyed.
  bool batch_upload_done = false;
  uploader->Submit([&batch_upload_done]() { batch_upload_done = true; });
  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(batch_upload_done);
}

VK_TEST_F(BatchGpuUploaderTest, CallbackTriggeredOnEmptyUploader) {
  auto escher = test::GetEscher()->GetWeakPtr();
  std::unique_ptr<BatchGpuUploader> uploader = BatchGpuUploader::New(escher);

  EXPECT_FALSE(uploader->HasContentToUpload());
  bool callback_executed = false;

  uploader->Submit([&callback_executed] { callback_executed = true; });
  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(callback_executed);
}

VK_TEST_F(BatchGpuUploaderTest, AcquireSubmitMultipleWriters) {
  auto escher = test::GetEscher()->GetWeakPtr();
  std::unique_ptr<BatchGpuUploader> uploader = BatchGpuUploader::New(escher);

  auto writer = uploader->AcquireWriter(256);
  uploader->PostWriter(std::move(writer));
  // CommandBuffer should not have been posted to the driver, cleanup should
  // fail.
  escher->vk_device().waitIdle();
  EXPECT_FALSE(escher->Cleanup());

  auto writer2 = uploader->AcquireWriter(256);
  uploader->PostWriter(std::move(writer2));
  // CommandBuffer should not have been posted to the driver, cleanup should
  // fail.
  escher->vk_device().waitIdle();
  EXPECT_FALSE(escher->Cleanup());

  bool batched_upload_done = false;
  uploader->Submit([&batched_upload_done]() { batched_upload_done = true; });
  // Trigger Cleanup, which triggers the callback on the submitted command
  // buffer.
  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(batched_upload_done);
}

VK_TEST_F(BatchGpuUploaderTest, WriteBuffer) {
  auto escher = test::GetEscher()->GetWeakPtr();
  auto uploader = BatchGpuUploader::New(escher);
  const size_t buffer_size = 3 * sizeof(vec3);
  auto writer = uploader->AcquireWriter(buffer_size);
  // Create buffer to write to.
  BufferFactoryAdapter buffer_factory(escher->gpu_allocator(), escher->resource_recycler());
  BufferPtr vertex_buffer = buffer_factory.NewBuffer(
      buffer_size, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
      vk::MemoryPropertyFlagBits::eDeviceLocal);

  // Do write.
  void* host_ptr = writer->host_ptr();
  vec3* const verts = static_cast<vec3*>(host_ptr);
  verts[0] = vec3(0.f, 0.f, 0.f);
  verts[1] = vec3(0.f, 1.f, 0.f);
  verts[2] = vec3(1.f, 0.f, 0.f);
  writer->WriteBuffer(vertex_buffer, {0, 0, vertex_buffer->size()});
  // Posting and submitting should succeed.
  uploader->PostWriter(std::move(writer));

  // Submit the work.
  bool batch_upload_done = false;
  uploader->Submit([&batch_upload_done]() { batch_upload_done = true; });
  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(batch_upload_done);
}

VK_TEST_F(BatchGpuUploaderTest, WriteImage) {
  auto escher = test::GetEscher()->GetWeakPtr();
  auto uploader = BatchGpuUploader::New(escher);
  const size_t image_size = sizeof(uint8_t) * 4;
  auto writer = uploader->AcquireWriter(image_size);

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
  constexpr vk::ImageLayout kTargetImageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  void* host_ptr = writer->host_ptr();
  uint8_t* const pixels = static_cast<uint8_t*>(host_ptr);
  pixels[0] = 150;
  pixels[1] = 88;
  pixels[2] = 121;
  pixels[3] = 255;
  writer->WriteImage(image, region, kTargetImageLayout);
  // Posting and submitting should succeed.
  uploader->PostWriter(std::move(writer));

  // Submit the work.
  bool batch_upload_done = false;
  uploader->Submit([&batch_upload_done]() { batch_upload_done = true; });

  escher->vk_device().waitIdle();
  // Verify that the image layout was set correctly.
  EXPECT_TRUE(image->layout() == kTargetImageLayout);
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(batch_upload_done);
}

VK_TEST_F(BatchGpuUploaderTest, WriterNotPostedFails) {
  auto escher = test::GetEscher()->GetWeakPtr();
  auto uploader = BatchGpuUploader::New(escher);

  // Submit should fail since it does not have the command buffer to submit.
  EXPECT_DEATH(
      {
        auto writer = uploader->AcquireWriter(256);
        uploader->Submit();
      },
      "");
}

VK_TEST_F(BatchGpuUploaderTest, WriterPostedToWrongUploaderFails) {
  auto escher = test::GetEscher()->GetWeakPtr();
  auto uploader = BatchGpuUploader::New(escher, 0);
  auto writer = uploader->AcquireWriter(256);

  // New uploader is created with a different backing frame. Posting the first
  // uploader's writer to this should fail.
  auto uploader2 = BatchGpuUploader::New(escher, 1);
  EXPECT_DEATH(uploader2->PostWriter(std::move(writer)), "");
  // Instead we should post the writer to the first uploader instead.
  EXPECT_NO_FATAL_FAILURE(uploader->PostWriter(std::move(writer)));

  // Old uploader should be able to be successfully submitted and cleaned up.
  bool batch_upload_done = false;
  uploader->Submit([&batch_upload_done]() { batch_upload_done = true; });
  // New uploader should be cleaned up as well since no PostWriter() was
  // actually executed when the test reaches here.
  bool batch_upload_done_2 = false;
  uploader2->Submit([&batch_upload_done_2]() { batch_upload_done_2 = true; });

  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(batch_upload_done && batch_upload_done_2);
}

}  // namespace escher
