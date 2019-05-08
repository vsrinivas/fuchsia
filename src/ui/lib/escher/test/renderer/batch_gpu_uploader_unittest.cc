// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"

#include <thread>

#include "src/ui/lib/escher/test/gtest_escher.h"
#include "src/ui/lib/escher/test/vk/vulkan_tester.h"
#include "gtest/gtest.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/vk/buffer_factory.h"
#include "src/ui/lib/escher/vk/image_factory.h"

namespace escher {

VK_TEST(BatchGpuUploader, CreateDestroyUploader) {
  auto escher = test::GetEscher()->GetWeakPtr();

  {
    std::unique_ptr<BatchGpuUploader> uploader = BatchGpuUploader::New(escher);
    // BatchGpuUploader must be submitted before it is destroyed.
    uploader->Submit();
  }

  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
}

VK_TEST(BatchGpuUploader, InvalidUploader) {
  // A BatchGpuUploader without an escher should not be created.
  auto uploader = BatchGpuUploader::New(EscherWeakPtr());
  EXPECT_FALSE(uploader);
}

VK_TEST(BatchGpuUploader, AcquireSubmitWriter) {
  auto escher = test::GetEscher()->GetWeakPtr();
  std::unique_ptr<BatchGpuUploader> uploader = BatchGpuUploader::New(escher);

  auto writer = uploader->AcquireWriter(256);
  uploader->PostWriter(std::move(writer));

  // BatchGpuUploader must be submitted before it is destroyed.
  uploader->Submit();
  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
}

VK_TEST(BatchGpuUploader, AcquireSubmitReader) {
  auto escher = test::GetEscher()->GetWeakPtr();
  std::unique_ptr<BatchGpuUploader> uploader = BatchGpuUploader::New(escher);

  auto reader = uploader->AcquireReader(256);
  uploader->PostReader(std::move(reader), [](BufferPtr) {});

  // BatchGpuUploader must be submitted before it is destroyed.
  uploader->Submit();
  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
}

VK_TEST(BatchGpuUploader, AcquireSubmitMultipleWriters) {
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

VK_TEST(BatchGpuUploader, AcquireSubmitMultipleReaders) {
  auto escher = test::GetEscher()->GetWeakPtr();
  std::unique_ptr<BatchGpuUploader> uploader = BatchGpuUploader::New(escher);

  auto reader = uploader->AcquireReader(256);
  uploader->PostReader(std::move(reader), [](BufferPtr) {});
  // CommandBuffer should not have been posted to the driver, cleanup should
  // fail.
  escher->vk_device().waitIdle();
  EXPECT_FALSE(escher->Cleanup());

  auto reader2 = uploader->AcquireReader(256);
  uploader->PostReader(std::move(reader2), [](BufferPtr) {});
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

VK_TEST(BatchGpuUploader, WriteBuffer) {
  auto escher = test::GetEscher()->GetWeakPtr();
  auto uploader = BatchGpuUploader::New(escher);
  const size_t buffer_size = 3 * sizeof(vec3);
  auto writer = uploader->AcquireWriter(buffer_size);
  // Create buffer to write to.
  BufferFactoryAdapter buffer_factory(escher->gpu_allocator(),
                                      escher->resource_recycler());
  BufferPtr vertex_buffer =
      buffer_factory.NewBuffer(buffer_size,
                               vk::BufferUsageFlagBits::eVertexBuffer |
                                   vk::BufferUsageFlagBits::eTransferDst,
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
  // Verify that the "upload done" Semaphore was set on the buffer.
  EXPECT_TRUE(vertex_buffer->HasWaitSemaphore());

  // Submit the work.
  uploader->Submit();
  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
}

VK_TEST(BatchGpuUploader, WriteImage) {
  auto escher = test::GetEscher()->GetWeakPtr();
  auto uploader = BatchGpuUploader::New(escher);
  const size_t image_size = sizeof(uint8_t) * 4;
  auto writer = uploader->AcquireWriter(image_size);

  // Create a 1x1 RGBA (8-bit channels) image to write to.
  ImageFactoryAdapter image_factory(escher->gpu_allocator(),
                                    escher->resource_recycler());
  ImagePtr image =
      image_utils::NewImage(&image_factory, vk::Format::eR8G8B8A8Unorm, 1, 1);
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
  uint8_t* const pixels = static_cast<uint8_t*>(host_ptr);
  pixels[0] = 150;
  pixels[1] = 88;
  pixels[2] = 121;
  pixels[3] = 255;
  EXPECT_FALSE(image->HasWaitSemaphore());
  writer->WriteImage(image, region);
  // Posting and submitting should succeed.
  uploader->PostWriter(std::move(writer));
  // Verify that the "upload done" Semaphore was set on the image.
  EXPECT_TRUE(image->HasWaitSemaphore());

  // Submit the work.
  uploader->Submit();
  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
}

VK_TEST(BatchGpuUploader, DISABLED_WriterNotPostedFails) {
  // This successfully tests death. However, when unsupported, death kills the
  // test process. Disabled until EXPECT_DEATH_IF_SUPPORTED can graduate to
  // EXPECT_DEATH.
  auto escher = test::GetEscher()->GetWeakPtr();
  auto uploader = BatchGpuUploader::New(escher);
  auto writer = uploader->AcquireWriter(256);

  // Submit should fail since it does not have the command buffer to submit.
  EXPECT_DEATH_IF_SUPPORTED(uploader->Submit(), "");
}

VK_TEST(BatchGpuUploader, DISABLED_WriterPostedToWrongUploaderFails) {
  // This successfully tests death. However, when unsupported, death kills the
  // test process. Disabled until EXPECT_DEATH_IF_SUPPORTED can graduate to
  // EXPECT_DEATH.
  auto escher = test::GetEscher()->GetWeakPtr();
  auto uploader = BatchGpuUploader::New(escher, 0);
  auto writer = uploader->AcquireWriter(256);

  // New uploader is created with a different backing frame. Posting the first
  // uploader's writer to this should fail.
  auto uploader2 = BatchGpuUploader::New(escher, 1);
  EXPECT_DEATH_IF_SUPPORTED(uploader->PostWriter(std::move(writer)), "");

  // New uploader should be able to be successfully submitted and cleaned up.
  uploader2->Submit();
  // Original uploader did not have writer posted, and should fail to submit or
  // be destroyed.
  EXPECT_DEATH_IF_SUPPORTED(uploader->Submit(), "");
}

VK_TEST(BatchGpuUploader, DISABLED_ReadAWriteSucceeds) {
  // TODO (SCN-1197) Enable once memory barriers are added to the
  // BatchGpuUploader and it can be used for reads and writes on the same
  // resource.

  auto escher = test::GetEscher()->GetWeakPtr();
  auto uploader = BatchGpuUploader::New(escher);
  const size_t buffer_size = 3 * sizeof(vec3);
  auto writer = uploader->AcquireWriter(buffer_size);
  // Create buffer to write to.
  BufferFactoryAdapter buffer_factory(escher->gpu_allocator(),
                                      escher->resource_recycler());
  BufferPtr vertex_buffer =
      buffer_factory.NewBuffer(buffer_size,
                               vk::BufferUsageFlagBits::eVertexBuffer |
                                   vk::BufferUsageFlagBits::eTransferSrc |
                                   vk::BufferUsageFlagBits::eTransferDst,
                               vk::MemoryPropertyFlagBits::eDeviceLocal);
  EXPECT_FALSE(vertex_buffer->HasWaitSemaphore());

  // Do write.
  void* host_ptr = writer->host_ptr();
  vec3* const write_verts = static_cast<vec3*>(host_ptr);
  write_verts[0] = vec3(0.f, 0.f, 1.f);
  write_verts[1] = vec3(0.f, 1.f, 0.f);
  write_verts[2] = vec3(1.f, 0.f, 0.f);
  writer->WriteBuffer(vertex_buffer, {0, 0, vertex_buffer->size()});
  // Posting and submitting should succeed.
  uploader->PostWriter(std::move(writer));
  // Verify that the "upload done" Semaphore was set on the buffer.
  EXPECT_TRUE(vertex_buffer->HasWaitSemaphore());

  // Create reader to read from the buffer pending a write.
  auto reader = uploader->AcquireReader(buffer_size);
  reader->ReadBuffer(vertex_buffer, {0, 0, vertex_buffer->size()});
  EXPECT_TRUE(vertex_buffer->HasWaitSemaphore());

  bool read_buffer_done = false;
  uploader->PostReader(std::move(reader),
                       [&read_buffer_done, &write_verts](BufferPtr buffer) {
                         void* host_ptr = buffer->host_ptr();
                         vec3* const read_verts = static_cast<vec3*>(host_ptr);
                         EXPECT_EQ(read_verts[0], write_verts[0]);
                         EXPECT_EQ(read_verts[1], write_verts[1]);
                         EXPECT_EQ(read_verts[2], write_verts[2]);

                         read_buffer_done = true;
                       });

  // Submit all the work.
  uploader->Submit();
  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(read_buffer_done);
}

VK_TEST(BatchGpuUploader, ReadImageTest) {
  auto escher = test::GetEscher()->GetWeakPtr();
  auto image = escher->NewNoiseImage(512, 512);

  vk::BufferImageCopy region;
  region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageExtent.width = image->width();
  region.imageExtent.height = image->height();
  region.imageExtent.depth = 1;
  region.bufferOffset = 0;

  BatchGpuUploader uploader(escher, 0);
  auto reader = uploader.AcquireReader(image->size());
  reader->ReadImage(image, region);
  // Verify that the "upload done" Semaphore was set on the image.
  EXPECT_TRUE(image->HasWaitSemaphore());

  bool read_image_done = false;
  uploader.PostReader(std::move(reader), [&read_image_done](BufferPtr buffer) {
    read_image_done = true;
  });

  uploader.Submit([]() {});

  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(read_image_done);
}

VK_TEST(BatchGpuUploader, ReadBufferTest) {
  auto escher = test::GetEscher()->GetWeakPtr();
  // Create buffer to read from.
  const size_t buffer_size = 3 * sizeof(vec3);
  BufferFactoryAdapter buffer_factory(escher->gpu_allocator(),
                                      escher->resource_recycler());
  BufferPtr vertex_buffer =
      buffer_factory.NewBuffer(buffer_size,
                               vk::BufferUsageFlagBits::eVertexBuffer |
                                   vk::BufferUsageFlagBits::eTransferSrc |
                                   vk::BufferUsageFlagBits::eTransferDst,
                               vk::MemoryPropertyFlagBits::eHostVisible);
  void* host_ptr = vertex_buffer->host_ptr();
  vec3* const verts = static_cast<vec3*>(host_ptr);
  verts[0] = vec3(0.f, 0.f, 0.f);
  verts[1] = vec3(0.f, 1.f, 0.f);
  verts[2] = vec3(1.f, 0.f, 0.f);

  // Do read.
  std::unique_ptr<BatchGpuUploader> uploader = BatchGpuUploader::New(escher, 0);
  auto reader = uploader->AcquireReader(buffer_size);
  reader->ReadBuffer(vertex_buffer, {0, 0, vertex_buffer->size()});
  // Verify that the "upload done" Semaphore was set on the buffer.
  EXPECT_TRUE(vertex_buffer->HasWaitSemaphore());

  bool read_buffer_done = false;
  uploader->PostReader(std::move(reader),
                       [&read_buffer_done](BufferPtr buffer) {
                         void* host_ptr = buffer->host_ptr();
                         vec3* const verts = static_cast<vec3*>(host_ptr);
                         EXPECT_EQ(verts[0], vec3(0.f, 0.f, 0.f));
                         EXPECT_EQ(verts[1], vec3(0.f, 1.f, 0.f));
                         EXPECT_EQ(verts[2], vec3(1.f, 0.f, 0.f));

                         read_buffer_done = true;
                       });
  uploader->Submit();

  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(read_buffer_done);
}

}  // namespace escher
