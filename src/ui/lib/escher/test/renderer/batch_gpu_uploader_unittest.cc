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

  {
    std::unique_ptr<BatchGpuUploader> uploader = BatchGpuUploader::New(escher);
    // BatchGpuUploader must be submitted before it is destroyed.
    uploader->Submit();
  }

  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
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
  uploader->Submit();
  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
}

VK_TEST_F(BatchGpuUploaderTest, AcquireSubmitReader) {
  auto escher = test::GetEscher()->GetWeakPtr();
  std::unique_ptr<BatchGpuUploader> uploader = BatchGpuUploader::New(escher);

  auto reader = uploader->AcquireReader(256);
  uploader->PostReader(std::move(reader), [](BufferPtr) {});

  // BatchGpuUploader must be submitted before it is destroyed.
  uploader->Submit();
  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
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

VK_TEST_F(BatchGpuUploaderTest, AcquireSubmitMultipleReaders) {
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

// Test to make sure that multiple readers can access the same
// buffer and still succesfully submit work to the GPU and have
// it finish. This is to make sure that the command buffer does
// not get stuck waiting on itself by accidentally giving itself
// its own signal semaphore to wait on.
VK_TEST_F(BatchGpuUploaderTest, MultipleReaderSameBuffer) {
  auto escher = test::GetEscher()->GetWeakPtr();
  std::unique_ptr<BatchGpuUploader> uploader = BatchGpuUploader::New(escher);

  // Create buffer to read from.
  const size_t buffer_size = 3 * sizeof(vec3);
  BufferFactoryAdapter buffer_factory(escher->gpu_allocator(), escher->resource_recycler());
  BufferPtr vertex_buffer = buffer_factory.NewBuffer(buffer_size,
                                                     vk::BufferUsageFlagBits::eVertexBuffer |
                                                         vk::BufferUsageFlagBits::eTransferSrc |
                                                         vk::BufferUsageFlagBits::eTransferDst,
                                                     vk::MemoryPropertyFlagBits::eHostVisible);
  // Create a reader and read the vertex buffer.
  auto reader = uploader->AcquireReader(vertex_buffer->size());
  reader->ReadBuffer(vertex_buffer, {0, 0, vertex_buffer->size()});
  uploader->PostReader(std::move(reader), [](BufferPtr) {});

  // Create a second reader and read the *same* vertex buffer.
  auto reader2 = uploader->AcquireReader(vertex_buffer->size());
  reader2->ReadBuffer(vertex_buffer, {0, 0, vertex_buffer->size()});
  uploader->PostReader(std::move(reader2), [](BufferPtr) {});

  // Create a third reader and read the *same* vertex buffer.
  auto reader3 = uploader->AcquireReader(vertex_buffer->size());
  reader3->ReadBuffer(vertex_buffer, {0, 0, vertex_buffer->size()});
  uploader->PostReader(std::move(reader3), [](BufferPtr) {});

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
  uploader->Submit();
  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
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
  void* host_ptr = writer->host_ptr();
  uint8_t* const pixels = static_cast<uint8_t*>(host_ptr);
  pixels[0] = 150;
  pixels[1] = 88;
  pixels[2] = 121;
  pixels[3] = 255;
  writer->WriteImage(image, region);
  // Posting and submitting should succeed.
  uploader->PostWriter(std::move(writer));

  // Submit the work.
  uploader->Submit();
  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
}

VK_TEST_F(BatchGpuUploaderTest, DISABLED_WriterNotPostedFails) {
  // This successfully tests death. However, when unsupported, death kills the
  // test process. Disabled until EXPECT_DEATH_IF_SUPPORTED can graduate to
  // EXPECT_DEATH.
  auto escher = test::GetEscher()->GetWeakPtr();
  auto uploader = BatchGpuUploader::New(escher);
  auto writer = uploader->AcquireWriter(256);

  // Submit should fail since it does not have the command buffer to submit.
  EXPECT_DEATH_IF_SUPPORTED(uploader->Submit(), "");
}

VK_TEST_F(BatchGpuUploaderTest, DISABLED_WriterPostedToWrongUploaderFails) {
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

VK_TEST_F(BatchGpuUploaderTest, DISABLED_ReadAWriteSucceeds) {
  // TODO (SCN-1197) Enable once memory barriers are added to the
  // BatchGpuUploader and it can be used for reads and writes on the same
  // resource.

  auto escher = test::GetEscher()->GetWeakPtr();
  auto uploader = BatchGpuUploader::New(escher);
  const size_t buffer_size = 3 * sizeof(vec3);
  auto writer = uploader->AcquireWriter(buffer_size);
  // Create buffer to write to.
  BufferFactoryAdapter buffer_factory(escher->gpu_allocator(), escher->resource_recycler());
  BufferPtr vertex_buffer = buffer_factory.NewBuffer(buffer_size,
                                                     vk::BufferUsageFlagBits::eVertexBuffer |
                                                         vk::BufferUsageFlagBits::eTransferSrc |
                                                         vk::BufferUsageFlagBits::eTransferDst,
                                                     vk::MemoryPropertyFlagBits::eDeviceLocal);

  // Do write.
  void* host_ptr = writer->host_ptr();
  vec3* const write_verts = static_cast<vec3*>(host_ptr);
  write_verts[0] = vec3(0.f, 0.f, 1.f);
  write_verts[1] = vec3(0.f, 1.f, 0.f);
  write_verts[2] = vec3(1.f, 0.f, 0.f);
  writer->WriteBuffer(vertex_buffer, {0, 0, vertex_buffer->size()});
  // Posting and submitting should succeed.
  uploader->PostWriter(std::move(writer));

  // Create reader to read from the buffer pending a write.
  auto reader = uploader->AcquireReader(buffer_size);
  reader->ReadBuffer(vertex_buffer, {0, 0, vertex_buffer->size()});

  bool read_buffer_done = false;
  uploader->PostReader(std::move(reader), [&read_buffer_done, &write_verts](BufferPtr buffer) {
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

VK_TEST_F(BatchGpuUploaderTest, ReadImageTest) {
  auto escher = test::GetEscher()->GetWeakPtr();

  // Upload an image to read back.
  constexpr uint32_t kWidth = 512;
  constexpr uint32_t kHeight = 256;
  auto pixels = image_utils::NewNoisePixels(kWidth, kHeight);
  auto image = image_utils::NewImage(escher->image_cache(), vk::Format::eR8Unorm, kWidth, kHeight);
  BatchGpuUploader uploader(escher, 0);
  image_utils::WritePixelsToImage(&uploader, pixels.get(), image,
                                  vk::ImageLayout::eTransferSrcOptimal);
  auto sema = escher::Semaphore::New(escher->vk_device());
  uploader.AddSignalSemaphore(sema);
  uploader.Submit();

  // Read back the uploaded pixels.
  vk::BufferImageCopy region;
  region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageExtent.width = image->width();
  region.imageExtent.height = image->height();
  region.imageExtent.depth = 1;
  region.bufferOffset = 0;

  BatchGpuUploader downloader(escher, 0);
  downloader.AddWaitSemaphore(sema, vk::PipelineStageFlagBits::eTransfer);
  auto reader = downloader.AcquireReader(image->size());
  reader->ReadImage(image, region);

  bool read_image_done = false;
  downloader.PostReader(std::move(reader), [&read_image_done, original = std::move(pixels),
                                            num_bytes = kWidth * kHeight](BufferPtr buffer) {
    bool pixels_match = !memcmp(original.get(), buffer->host_ptr(), num_bytes);
    EXPECT_TRUE(pixels_match);
    read_image_done = true;
  });

  downloader.Submit([]() {});

  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(read_image_done);
}

VK_TEST_F(BatchGpuUploaderTest, ReadBufferTest) {
  auto escher = test::GetEscher()->GetWeakPtr();
  // Create buffer to read from.
  const size_t buffer_size = 3 * sizeof(vec3);
  BufferFactoryAdapter buffer_factory(escher->gpu_allocator(), escher->resource_recycler());
  BufferPtr vertex_buffer = buffer_factory.NewBuffer(buffer_size,
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

  bool read_buffer_done = false;
  uploader->PostReader(std::move(reader), [&read_buffer_done](BufferPtr buffer) {
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
