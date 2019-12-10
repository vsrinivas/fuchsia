// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"

#include <thread>

#include "gtest/gtest.h"
#include "src/ui/lib/escher/renderer/batch_gpu_downloader.h"
#include "src/ui/lib/escher/test/gtest_escher.h"
#include "src/ui/lib/escher/test/vk/vulkan_tester.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/vk/buffer_factory.h"
#include "src/ui/lib/escher/vk/command_buffer.h"
#include "src/ui/lib/escher/vk/image_factory.h"

namespace escher {

namespace {

std::pair<ImagePtr, vk::BufferImageCopy> Create1x1ImageAndRegion(EscherWeakPtr escher) {
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
  return {std::move(image), std::move(region)};
}

}  // namespace

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

VK_TEST_F(BatchGpuUploaderTest, WriteBufferUsingWriteFunction) {
  auto escher = test::GetEscher()->GetWeakPtr();
  auto uploader = BatchGpuUploader::New(escher);
  const size_t buffer_size = 3 * sizeof(vec3);
  // Create buffer to write to.
  BufferFactoryAdapter buffer_factory(escher->gpu_allocator(), escher->resource_recycler());
  BufferPtr vertex_buffer = buffer_factory.NewBuffer(
      buffer_size, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
      vk::MemoryPropertyFlagBits::eDeviceLocal);

  // Do write.
  bool write_finished = false;
  uploader->ScheduleWriteBuffer(
      vertex_buffer,
      [&write_finished](uint8_t* host_ptr, size_t size) {
        EXPECT_TRUE(size >= sizeof(vec3) * 3);
        vec3* verts = reinterpret_cast<vec3*>(host_ptr);
        verts[0] = vec3(0.f, 0.f, 0.f);
        verts[1] = vec3(0.f, 1.f, 0.f);
        verts[2] = vec3(1.f, 0.f, 0.f);
        write_finished = true;
      },
      /* target_offset */ 0, /* copy_size */ buffer_size);
  // The write is deferred until we generate the commands.
  EXPECT_FALSE(write_finished);

  // Submit the work.
  bool batch_upload_done = false;
  uploader->Submit([&batch_upload_done]() { batch_upload_done = true; });
  EXPECT_TRUE(write_finished);

  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(batch_upload_done);
}

VK_TEST_F(BatchGpuUploaderTest, WriteBufferUsingVectorOfUint8) {
  auto escher = test::GetEscher()->GetWeakPtr();
  auto uploader = BatchGpuUploader::New(escher);
  const size_t buffer_size = 3 * sizeof(vec3);
  // Create buffer to write to.
  BufferFactoryAdapter buffer_factory(escher->gpu_allocator(), escher->resource_recycler());
  BufferPtr vertex_buffer = buffer_factory.NewBuffer(
      buffer_size, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
      vk::MemoryPropertyFlagBits::eDeviceLocal);

  // Do write.
  std::vector<uint8_t> host_data(buffer_size);
  vec3* verts = reinterpret_cast<vec3*>(host_data.data());
  verts[0] = vec3(0.f, 0.f, 0.f);
  verts[1] = vec3(0.f, 1.f, 0.f);
  verts[2] = vec3(1.f, 0.f, 0.f);
  uploader->ScheduleWriteBuffer(vertex_buffer, std::move(host_data));

  // Submit the work.
  bool batch_upload_done = false;
  uploader->Submit([&batch_upload_done]() { batch_upload_done = true; });
  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(batch_upload_done);
}

VK_TEST_F(BatchGpuUploaderTest, WriteBufferUsingVectorOfAnyType) {
  auto escher = test::GetEscher()->GetWeakPtr();
  auto uploader = BatchGpuUploader::New(escher);
  const size_t buffer_size = 3 * sizeof(vec3);
  // Create buffer to write to.
  BufferFactoryAdapter buffer_factory(escher->gpu_allocator(), escher->resource_recycler());
  BufferPtr vertex_buffer = buffer_factory.NewBuffer(
      buffer_size, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
      vk::MemoryPropertyFlagBits::eDeviceLocal);

  // Do write.
  std::vector<vec3> verts(3);
  verts[0] = vec3(0.f, 0.f, 0.f);
  verts[1] = vec3(0.f, 1.f, 0.f);
  verts[2] = vec3(1.f, 0.f, 0.f);

  uploader->ScheduleWriteBuffer(vertex_buffer, verts);

  // Submit the work.
  bool batch_upload_done = false;
  uploader->Submit([&batch_upload_done]() { batch_upload_done = true; });
  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(batch_upload_done);
}

VK_TEST_F(BatchGpuUploaderTest, LazyInitializationTest) {
  auto escher = test::GetEscher()->GetWeakPtr();
  std::unique_ptr<BatchGpuUploader> uploader = BatchGpuUploader::New(escher);

  constexpr vk::DeviceSize kBufferSize = 1024;

  BufferFactoryAdapter buffer_factory(escher->gpu_allocator(), escher->resource_recycler());
  BufferPtr buffer = buffer_factory.NewBuffer(
      kBufferSize, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
      vk::MemoryPropertyFlagBits::eDeviceLocal);

  // BatchGpuUploader will not be initialized until instantiation of Writers.
  EXPECT_FALSE(uploader->HasContentToUpload());

  std::vector<uint8_t> host_data(kBufferSize, 0x7f);
  uploader->ScheduleWriteBuffer(buffer, std::move(host_data));

  EXPECT_TRUE(uploader->HasContentToUpload());

  // BatchGpuUploader must be submitted before it is destroyed.
  bool batch_upload_done = false;
  uploader->Submit([&batch_upload_done]() { batch_upload_done = true; });
  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(batch_upload_done);
}

VK_TEST_F(BatchGpuUploaderTest, WriteImageUsingWriteFunction) {
  auto escher = test::GetEscher()->GetWeakPtr();
  auto uploader = BatchGpuUploader::New(escher);
  const size_t image_size = sizeof(uint8_t) * 4;

  // Create a 1x1 RGBA (8-bit channels) image to write to.
  auto [image, region] = Create1x1ImageAndRegion(escher);

  // Do write.
  constexpr vk::ImageLayout kTargetImageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  bool write_finished = false;
  uploader->ScheduleWriteImage(
      image,
      [&write_finished](uint8_t* host_ptr, size_t data_size) {
        EXPECT_TRUE(data_size >= 4);
        host_ptr[0] = 150;
        host_ptr[1] = 88;
        host_ptr[2] = 121;
        host_ptr[3] = 255;
        write_finished = true;
      },
      kTargetImageLayout);
  // The write is deferred until we generate the commands.
  EXPECT_FALSE(write_finished);

  // Submit the work.
  bool batch_upload_done = false;
  uploader->Submit([&batch_upload_done]() { batch_upload_done = true; });
  EXPECT_TRUE(write_finished);

  escher->vk_device().waitIdle();
  // Verify that the image layout was set correctly.
  EXPECT_TRUE(image->layout() == kTargetImageLayout);
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(batch_upload_done);
}

VK_TEST_F(BatchGpuUploaderTest, WriteImageUsingVectorOfUint8) {
  auto escher = test::GetEscher()->GetWeakPtr();
  auto uploader = BatchGpuUploader::New(escher);
  const size_t image_size = sizeof(uint8_t) * 4;

  // Create a 1x1 RGBA (8-bit channels) image to write to.
  auto [image, region] = Create1x1ImageAndRegion(escher);

  // Do write.
  constexpr vk::ImageLayout kTargetImageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  std::vector<uint8_t> pixels(image_size);
  pixels[0] = 150;
  pixels[1] = 88;
  pixels[2] = 121;
  pixels[3] = 255;
  uploader->ScheduleWriteImage(image, std::move(pixels), kTargetImageLayout, region);

  // Submit the work.
  bool batch_upload_done = false;
  uploader->Submit([&batch_upload_done]() { batch_upload_done = true; });

  escher->vk_device().waitIdle();
  // Verify that the image layout was set correctly.
  EXPECT_TRUE(image->layout() == kTargetImageLayout);
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(batch_upload_done);
}

VK_TEST_F(BatchGpuUploaderTest, WriteImageUsingVectorOfAnyType) {
  struct RGBA {
    uint8_t r, g, b, a;
  };

  auto escher = test::GetEscher()->GetWeakPtr();
  auto uploader = BatchGpuUploader::New(escher);

  // Create a 1x1 RGBA (8-bit channels) image to write to.
  auto [image, region] = Create1x1ImageAndRegion(escher);

  // Do write.
  constexpr vk::ImageLayout kTargetImageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  std::vector<RGBA> pixels;
  pixels.push_back({150, 88, 121, 255});
  uploader->ScheduleWriteImage(image, std::move(pixels), kTargetImageLayout, region);

  // Submit the work.
  bool batch_upload_done = false;
  uploader->Submit([&batch_upload_done]() { batch_upload_done = true; });

  escher->vk_device().waitIdle();
  // Verify that the image layout was set correctly.
  EXPECT_TRUE(image->layout() == kTargetImageLayout);
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(batch_upload_done);
}

// This unit tests if we can upload to the same image multiple times and if
// the image layout can be set correctly every time we upload the image.
VK_TEST_F(BatchGpuUploaderTest, ChangeLayout) {
  auto escher = test::GetEscher()->GetWeakPtr();

  // Create a 1x1 RGBA (8-bit channels) image to write to.
  std::vector<uint8_t> pixel_1 = {150, 88, 121, 255};
  auto [image, region] = Create1x1ImageAndRegion(escher);

  // Do write.
  auto uploader = BatchGpuUploader::New(escher);
  constexpr vk::ImageLayout kTargetImageLayout = vk::ImageLayout::eGeneral;
  uploader->ScheduleWriteImage(image, std::move(pixel_1), kTargetImageLayout, region);

  // Submit the work.
  bool batch_upload_done = false;
  uploader->Submit([&batch_upload_done]() { batch_upload_done = true; });
  escher->vk_device().waitIdle();
  EXPECT_TRUE(image->layout() == kTargetImageLayout);
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(batch_upload_done);

  // Write the image again and change the image layout to another layout.
  auto uploader_2 = BatchGpuUploader::New(escher);
  constexpr vk::ImageLayout kTargetImageLayout_2 = vk::ImageLayout::eShaderReadOnlyOptimal;
  std::vector<uint8_t> pixel_2 = {130, 120, 110, 255};
  uploader_2->ScheduleWriteImage(image, std::move(pixel_2), kTargetImageLayout_2, region);

  // Submit the work.
  bool batch_upload_done_2 = false;
  uploader_2->Submit([&batch_upload_done_2]() { batch_upload_done_2 = true; });
  escher->vk_device().waitIdle();

  // Verify that the image layout was set correctly.
  EXPECT_TRUE(image->layout() == kTargetImageLayout_2);
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(batch_upload_done_2);
}

VK_TEST_F(BatchGpuUploaderTest, SubmitImageToCommandBuffer) {
  auto escher = test::GetEscher()->GetWeakPtr();

  // Create a 1x1 RGBA (8-bit channels) image to write to.
  std::vector<uint8_t> pixel_1 = {150, 88, 121, 255};
  auto [image, region] = Create1x1ImageAndRegion(escher);

  // Do write.
  auto uploader = BatchGpuUploader::New(escher);
  constexpr vk::ImageLayout kTargetImageLayout = vk::ImageLayout::eGeneral;
  uploader->ScheduleWriteImage(image, std::move(pixel_1), kTargetImageLayout, region);

  auto cmds = CommandBuffer::NewForTransfer(escher.get());
  uploader->GenerateCommands(cmds.get());
  bool uploaded = false;
  cmds->Submit([&uploaded]() { uploaded = true; });
  EXPECT_FALSE(uploader->HasContentToUpload());

  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(uploaded);

  // Check if the uploaded content is correct.
  auto downloader = BatchGpuDownloader::New(escher);
  bool pixel_correct = false;
  downloader->ScheduleReadImage(image, region, [&pixel_correct](const void* host_ptr, size_t size) {
    const auto* pixel_downloaded = reinterpret_cast<const uint8_t*>(host_ptr);
    pixel_correct = pixel_downloaded[0] == 150u && pixel_downloaded[1] == 88u &&
                    pixel_downloaded[2] == 121u && pixel_downloaded[3] == 255u;
  });
  bool downloaded = false;
  downloader->Submit([&downloaded]() { downloaded = true; });

  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(downloaded && pixel_correct);
}

VK_TEST_F(BatchGpuUploaderTest, ReuseAfterSubmission) {
  auto escher = test::GetEscher()->GetWeakPtr();

  // Create a 1x1 RGBA (8-bit channels) image to write to.
  std::vector<uint8_t> pixel_1 = {150, 88, 121, 255};
  std::vector<uint8_t> pixel_2 = {130, 120, 110, 255};
  auto [image_1, region_1] = Create1x1ImageAndRegion(escher);
  auto [image_2, region_2] = Create1x1ImageAndRegion(escher);

  // Do write.
  auto uploader = BatchGpuUploader::New(escher);
  constexpr vk::ImageLayout kTargetImageLayout = vk::ImageLayout::eGeneral;
  uploader->ScheduleWriteImage(image_1, std::move(pixel_1), kTargetImageLayout, region_1);
  EXPECT_TRUE(uploader->HasContentToUpload());

  bool uploaded_1 = false;
  uploader->Submit([&uploaded_1]() { uploaded_1 = true; });
  EXPECT_FALSE(uploader->HasContentToUpload());

  // Schedule another write after submission.
  uploader->ScheduleWriteImage(image_2, std::move(pixel_2), kTargetImageLayout, region_2);
  EXPECT_TRUE(uploader->HasContentToUpload());

  bool uploaded_2 = false;
  uploader->Submit([&uploaded_2]() { uploaded_2 = true; });
  EXPECT_FALSE(uploader->HasContentToUpload());

  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(uploaded_1 && uploaded_2);
}

VK_TEST_F(BatchGpuUploaderTest, UnfinishedWorkDeathTest) {
  auto escher = test::GetEscher()->GetWeakPtr();

  auto [image, region] = Create1x1ImageAndRegion(escher);
  std::vector<uint8_t> pixel_1 = {150, 88, 121, 255};
  constexpr vk::ImageLayout kTargetImageLayout = vk::ImageLayout::eGeneral;

  // Submit should fail since we scheduled image write but didn't submit that.
  EXPECT_DEATH(
      {
        auto uploader = BatchGpuUploader::New(escher);
        uploader->ScheduleWriteImage(image, std::move(pixel_1), kTargetImageLayout, region);
        uploader = nullptr;
      },
      "");
}

}  // namespace escher
