// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/renderer/batch_gpu_downloader.h"

#include <thread>

#include "gtest/gtest.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/test/gtest_escher.h"
#include "src/ui/lib/escher/test/vk/vulkan_tester.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/vk/buffer_factory.h"
#include "src/ui/lib/escher/vk/image_factory.h"
#include "test/test_with_vk_validation_layer_macros.h"
#include "third_party/granite/vk/command_buffer.h"

namespace escher {

using BatchGpuDownloaderTest = test::TestWithVkValidationLayer;

VK_TEST_F(BatchGpuDownloaderTest, CreateDestroyDownloader) {
  auto escher = test::GetEscher()->GetWeakPtr();
  bool batch_download_done = false;

  {
    std::unique_ptr<BatchGpuDownloader> downloader = BatchGpuDownloader::New(escher);
    // BatchGpuDownloader must be submitted before it is destroyed.
    downloader->Submit([&batch_download_done]() { batch_download_done = true; });
  }

  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(batch_download_done);
}

VK_TEST_F(BatchGpuDownloaderTest, InvalidDownloader) {
  // A BatchGpuDownloader without an escher should not be created.
  auto downloader = BatchGpuDownloader::New(EscherWeakPtr());
  EXPECT_FALSE(downloader);
}

VK_TEST_F(BatchGpuDownloaderTest, CallbackTriggeredOnEmptyDownloader) {
  auto escher = test::GetEscher()->GetWeakPtr();
  std::unique_ptr<BatchGpuDownloader> downloader = BatchGpuDownloader::New(escher);

  EXPECT_FALSE(downloader->HasContentToDownload());
  bool callback_executed = false;

  downloader->Submit([&callback_executed] { callback_executed = true; });
  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(callback_executed);
}

VK_TEST_F(BatchGpuDownloaderTest, AcquireThenSubmitReader) {
  auto escher = test::GetEscher()->GetWeakPtr();
  std::unique_ptr<BatchGpuDownloader> downloader = BatchGpuDownloader::New(escher);

  auto reader = downloader->AcquireReader(256);
  bool reader_done = false;
  downloader->PostReader(std::move(reader), [&reader_done](BufferPtr) { reader_done = true; });

  // BatchGpuDownloader must be submitted before it is destroyed.
  bool batch_download_done = false;
  downloader->Submit([&batch_download_done]() { batch_download_done = true; });

  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(batch_download_done);
  EXPECT_TRUE(reader_done);
}

VK_TEST_F(BatchGpuDownloaderTest, LazyInitializationTest) {
  auto escher = test::GetEscher()->GetWeakPtr();
  std::unique_ptr<BatchGpuDownloader> downloader = BatchGpuDownloader::New(escher);

  // BatchGpuDownloader will not be initialized until instantiation of Readers.
  EXPECT_FALSE(downloader->HasContentToDownload());

  auto reader = downloader->AcquireReader(256);
  bool reader_done = false;
  downloader->PostReader(std::move(reader), [&reader_done](BufferPtr) { reader_done = true; });

  EXPECT_TRUE(downloader->HasContentToDownload());

  // BatchGpuDownloader must be submitted before it is destroyed.
  bool batch_download_done = false;
  downloader->Submit([&batch_download_done]() { batch_download_done = true; });
  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(batch_download_done);
  EXPECT_TRUE(reader_done);
}

VK_TEST_F(BatchGpuDownloaderTest, SupportAllCommandBufferTypes) {
  const std::vector<CommandBuffer::Type> kCommandBufferTypes = {CommandBuffer::Type::kTransfer,
                                                                CommandBuffer::Type::kCompute,
                                                                CommandBuffer::Type::kGraphics};
  std::vector<bool> downloaders_done = {false, false, false};

  auto escher = test::GetEscher()->GetWeakPtr();

  for (size_t i = 0; i < downloaders_done.size(); ++i) {
    const auto command_buffer_type = kCommandBufferTypes[i];
    std::unique_ptr<BatchGpuDownloader> downloader =
        BatchGpuDownloader::New(escher, command_buffer_type);
    auto reader = downloader->AcquireReader(256);
    downloader->PostReader(std::move(reader), [](BufferPtr) {});
    downloader->Submit([done_ptr = &downloaders_done[i]]() { *done_ptr = true; });
  }

  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  for (size_t i = 0; i < downloaders_done.size(); ++i) {
    EXPECT_TRUE(downloaders_done[i]);
  }
}

VK_TEST_F(BatchGpuDownloaderTest, AcquireThenSubmitMultipleReaders) {
  auto escher = test::GetEscher()->GetWeakPtr();
  std::unique_ptr<BatchGpuDownloader> downloader = BatchGpuDownloader::New(escher);

  auto reader = downloader->AcquireReader(256);
  downloader->PostReader(std::move(reader), [](BufferPtr) {});
  // CommandBuffer should not have been posted to the driver, cleanup should
  // fail.
  escher->vk_device().waitIdle();
  EXPECT_FALSE(escher->Cleanup());

  auto reader2 = downloader->AcquireReader(256);
  downloader->PostReader(std::move(reader2), [](BufferPtr) {});
  // CommandBuffer should not have been posted to the driver, cleanup should
  // fail.
  escher->vk_device().waitIdle();
  EXPECT_FALSE(escher->Cleanup());

  bool batched_download_done = false;
  downloader->Submit([&batched_download_done]() { batched_download_done = true; });
  // Trigger Cleanup, which triggers the callback on the submitted command
  // buffer.
  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(batched_download_done);
}

VK_TEST_F(BatchGpuDownloaderTest, InitializeUploaderAndDownloader) {
  // This test case tests if BatchGpuUploader and BatchGpuDownloader can
  // coexist with each other.
  auto escher = test::GetEscher()->GetWeakPtr();

  auto uploader = BatchGpuUploader::New(escher);
  auto downloader = BatchGpuDownloader::New(escher);

  auto writer = uploader->AcquireWriter(256);
  auto reader = downloader->AcquireReader(256);
  bool reader_done = false;

  uploader->PostWriter(std::move(writer));
  downloader->PostReader(std::move(reader), [&reader_done](BufferPtr) { reader_done = true; });

  bool uploader_finished = false;
  bool batch_download_done = false;
  uploader->Submit([&uploader_finished]() { uploader_finished = true; });
  downloader->Submit([&batch_download_done]() { batch_download_done = true; });

  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(uploader_finished);
  EXPECT_TRUE(batch_download_done);
  EXPECT_TRUE(reader_done);
}

VK_TEST_F(BatchGpuDownloaderTest, ReadImageTest) {
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

  vk::BufferImageCopy region;
  region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageExtent.width = image->width();
  region.imageExtent.height = image->height();
  region.imageExtent.depth = 1;
  region.bufferOffset = 0;

  BatchGpuDownloader downloader(escher, CommandBuffer::Type::kGraphics, 0);
  downloader.AddWaitSemaphore(sema, vk::PipelineStageFlagBits::eTransfer);
  auto reader = downloader.AcquireReader(image->size());
  reader->ReadImage(image, region);
  // Verify that the "download done" Semaphore was set on the image.

  bool read_image_done = false;
  downloader.PostReader(std::move(reader), [&read_image_done, original = std::move(pixels),
                                            num_bytes = kWidth * kHeight](BufferPtr buffer) {
    bool pixels_match = !memcmp(original.get(), buffer->host_ptr(), num_bytes);
    EXPECT_TRUE(pixels_match);
    read_image_done = true;
  });

  bool batch_download_done = false;
  downloader.Submit([&batch_download_done]() { batch_download_done = true; });

  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(read_image_done);
  EXPECT_TRUE(batch_download_done);
}

// For each Read() operation of BatchGpuDownloader::Reader, the Reader will
// keep the image layout if image layout is initialized; otherwise it will set
// image layout to eShaderReadOnlyOptimal.
//
// This test is created to make sure that the image layout is always set
// correctly, and we can do GPU image download no matter which layout the image
// has.
//
// In this test we first read the image and then submit the BatchGpuDownloader,
// then we read this image again using another BatchGpuDownloader to see if it
// works correctly.
VK_TEST_F(BatchGpuDownloaderTest, ReadTheSameImageTwice) {
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

  vk::BufferImageCopy region;
  region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageExtent.width = image->width();
  region.imageExtent.height = image->height();
  region.imageExtent.depth = 1;
  region.bufferOffset = 0;

  // We first read the image to a buffer.
  BatchGpuDownloader downloader(escher, CommandBuffer::Type::kGraphics, 0);
  downloader.AddWaitSemaphore(sema, vk::PipelineStageFlagBits::eTransfer);
  auto reader = downloader.AcquireReader(image->size());
  reader->ReadImage(image, region);
  // Verify that the "download done" Semaphore was set on the image.

  bool read_image_done = false;
  downloader.PostReader(std::move(reader),
                        [&read_image_done](BufferPtr buffer) { read_image_done = true; });
  bool batch_download_done = false;
  downloader.Submit([&batch_download_done]() { batch_download_done = true; });

  // After submitting the downloader command queue, read the same image again.
  // In this case Reader will do eShaderReadOnlyOptimal -> eTransferSrc
  // and eTransferSrc -> eShaderReadOnlyOptimal conversion.
  BatchGpuDownloader downloader2(escher, CommandBuffer::Type::kGraphics, 0);
  downloader2.AddWaitSemaphore(sema, vk::PipelineStageFlagBits::eTransfer);
  auto reader2 = downloader2.AcquireReader(image->size());
  reader2->ReadImage(image, region);

  bool read_image_done_2 = false;
  downloader2.PostReader(std::move(reader2),
                         [&read_image_done_2](BufferPtr buffer) { read_image_done_2 = true; });
  bool batch_download_done_2 = false;
  downloader2.Submit([&batch_download_done_2]() { batch_download_done_2 = true; });

  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(read_image_done && read_image_done_2);
  EXPECT_TRUE(batch_download_done && batch_download_done_2);
}

VK_TEST_F(BatchGpuDownloaderTest, ReadBufferTest) {
  auto escher = test::GetEscher()->GetWeakPtr();
  // Create buffer to read from.
  const size_t buffer_size = 3 * sizeof(vec3);
  BufferFactoryAdapter buffer_factory(escher->gpu_allocator(), escher->resource_recycler());
  BufferPtr vertex_buffer = buffer_factory.NewBuffer(
      buffer_size,
      vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferSrc |
          vk::BufferUsageFlagBits::eTransferDst,
      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

  // If VmaAllocator cannot allocate a buffer with both HostVisible and
  // HostCoherent memory property (required for a host buffer which is
  // able to be modified by the host), we will terminate the test.
  if (!vertex_buffer) {
    FXL_LOG(ERROR) << "Memory property not supported, test terminated.";
    EXPECT_TRUE(escher->Cleanup());
    return;
  }

  void* host_ptr = vertex_buffer->host_ptr();
  vec3* const verts = static_cast<vec3*>(host_ptr);
  verts[0] = vec3(0.f, 0.f, 0.f);
  verts[1] = vec3(0.f, 1.f, 0.f);
  verts[2] = vec3(1.f, 0.f, 0.f);

  // Do read.
  std::unique_ptr<BatchGpuDownloader> downloader =
      BatchGpuDownloader::New(escher, CommandBuffer::Type::kTransfer);
  auto reader = downloader->AcquireReader(buffer_size);
  reader->ReadBuffer(vertex_buffer, {0, 0, vertex_buffer->size()});

  bool read_buffer_done = false;
  downloader->PostReader(std::move(reader), [&read_buffer_done](BufferPtr buffer) {
    void* host_ptr = buffer->host_ptr();
    vec3* const verts = static_cast<vec3*>(host_ptr);
    EXPECT_EQ(verts[0], vec3(0.f, 0.f, 0.f));
    EXPECT_EQ(verts[1], vec3(0.f, 1.f, 0.f));
    EXPECT_EQ(verts[2], vec3(1.f, 0.f, 0.f));

    read_buffer_done = true;
  });
  bool batch_download_done = false;
  downloader->Submit([&batch_download_done]() { batch_download_done = true; });

  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(read_buffer_done);
  EXPECT_TRUE(batch_download_done);
}

// Test to make sure that multiple readers can access the same
// buffer and still succesfully submit work to the GPU and have
// it finish. This is to make sure that the command buffer does
// not get stuck waiting on itself.
VK_TEST_F(BatchGpuDownloaderTest, MultipleReaderSameBuffer) {
  auto escher = test::GetEscher()->GetWeakPtr();
  std::unique_ptr<BatchGpuDownloader> downloader = BatchGpuDownloader::New(escher);

  // Create buffer to read from.
  const size_t buffer_size = 3 * sizeof(vec3);
  BufferFactoryAdapter buffer_factory(escher->gpu_allocator(), escher->resource_recycler());
  BufferPtr vertex_buffer = buffer_factory.NewBuffer(buffer_size,
                                                     vk::BufferUsageFlagBits::eVertexBuffer |
                                                         vk::BufferUsageFlagBits::eTransferSrc |
                                                         vk::BufferUsageFlagBits::eTransferDst,
                                                     vk::MemoryPropertyFlagBits::eDeviceLocal);
  ASSERT_TRUE(vertex_buffer) << "Fatal: Cannot allocate device-local vertex buffer.";

  // Create a reader and read the vertex buffer.
  bool reader_callback_executed = false;
  auto reader = downloader->AcquireReader(vertex_buffer->size());
  reader->ReadBuffer(vertex_buffer, {0, 0, vertex_buffer->size()});
  downloader->PostReader(std::move(reader), [&reader_callback_executed](BufferPtr) {
    reader_callback_executed = true;
  });

  // Create a second reader and read the *same* vertex buffer.
  bool reader2_callback_executed = false;
  auto reader2 = downloader->AcquireReader(vertex_buffer->size());
  reader2->ReadBuffer(vertex_buffer, {0, 0, vertex_buffer->size()});
  downloader->PostReader(std::move(reader2), [&reader2_callback_executed](BufferPtr) {
    reader2_callback_executed = true;
  });

  // Create a third reader and read the *same* vertex buffer.
  bool reader3_callback_executed = false;
  auto reader3 = downloader->AcquireReader(vertex_buffer->size());
  reader3->ReadBuffer(vertex_buffer, {0, 0, vertex_buffer->size()});
  downloader->PostReader(std::move(reader3), [&reader3_callback_executed](BufferPtr) {
    reader3_callback_executed = true;
  });

  bool batch_download_done = false;
  downloader->Submit([&batch_download_done]() { batch_download_done = true; });

  // Trigger Cleanup, which triggers the callback on the submitted command
  // buffer.
  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(batch_download_done);
  EXPECT_TRUE(reader_callback_executed);
  EXPECT_TRUE(reader2_callback_executed);
  EXPECT_TRUE(reader3_callback_executed);
}

VK_TEST_F(BatchGpuDownloaderTest, DISABLED_ReadAfterWriteSucceeds) {
  // TODO(SCN-1197) Enable once memory barriers are added to the
  // BatchGpuDownloader and it can be used for reads and writes on the same
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
  ASSERT_TRUE(vertex_buffer) << "Fatal: Cannot allocate device-local vertex buffer.";

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
  auto downloader = BatchGpuDownloader::New(escher, CommandBuffer::Type::kTransfer);
  auto reader = downloader->AcquireReader(buffer_size);
  reader->ReadBuffer(vertex_buffer, {0, 0, vertex_buffer->size()});

  bool read_buffer_done = false;
  downloader->PostReader(std::move(reader), [&read_buffer_done, &write_verts](BufferPtr buffer) {
    void* host_ptr = buffer->host_ptr();
    vec3* const read_verts = static_cast<vec3*>(host_ptr);
    EXPECT_EQ(read_verts[0], write_verts[0]);
    EXPECT_EQ(read_verts[1], write_verts[1]);
    EXPECT_EQ(read_verts[2], write_verts[2]);

    read_buffer_done = true;
  });

  // Submit all the work.
  bool batch_download_done = false;
  downloader->Submit([&batch_download_done]() { batch_download_done = true; });
  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(read_buffer_done);
  EXPECT_TRUE(batch_download_done);
}

}  // namespace escher
