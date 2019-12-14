// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/test/fixtures/readback_test.h"

#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/renderer/frame.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/vk/image_factory.h"

namespace escher {

void ReadbackTest::SetUp() {
  escher_ = test::GetEscher()->GetWeakPtr();

  ImageFactoryAdapter image_factory(escher_->gpu_allocator(), escher_->resource_recycler());

  color_attachment_ = image_utils::NewImage(
      &image_factory, kColorFormat, kFramebufferWidth, kFramebufferHeight,
      vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc |
          vk::ImageUsageFlagBits::eTransferDst);

  auto depth_attachment_format_result = escher_->device()->caps().GetMatchingDepthStencilFormat();
  FXL_CHECK(depth_attachment_format_result.result == vk::Result::eSuccess)
      << "No depth stencil format is supported on this device.";
  depth_attachment_ =
      image_utils::NewDepthImage(&image_factory, depth_attachment_format_result.value,
                                 kFramebufferWidth, kFramebufferHeight, vk::ImageUsageFlags());

  // Create 1-pixel black image that will be used for clearing the framebuffer.
  // See NewFrame() for details.
  {
    auto uploader = BatchGpuUploader::New(escher_);
    uint8_t kBlack[] = {0, 0, 0, 255};
    black_ = image_utils::NewRgbaImage(&image_factory, uploader.get(), 1, 1, kBlack,
                                       vk::ImageLayout::eTransferSrcOptimal);
    uploader->Submit();
    escher_->vk_device().waitIdle();
  }

  // |readback_buffer_| contains the data that is read back from
  // |color_attachment_| to verify the correctness of its contents.
  readback_buffer_ = escher_->buffer_cache()->NewHostBuffer(kFramebufferWidth * kFramebufferHeight *
                                                            kFramebufferBytesPerPixel);

  frame_number_ = 0;
}

void ReadbackTest::TearDown() {
  escher_.reset();
  color_attachment_.reset();
  depth_attachment_.reset();
  black_.reset();
  readback_buffer_.reset();
}

ReadbackTest::FrameData ReadbackTest::NewFrame(vk::ImageLayout framebuffer_layout) {
  auto frame = escher_->NewFrame("ReadbackTest", ++frame_number_);
  CommandBuffer* cb = frame->cmds();

  // Wait for all previous commands to finish before clearing the image to
  // black.  We do this by blitting, because clearing a color attachment can
  // only be done during a render-pass.  We're not in a render-pass yet, and
  // there may not even be one.
  cb->ImageBarrier(color_attachment_, vk::ImageLayout::eUndefined,
                   vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits::eAllCommands,
                   vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eTransferWrite,
                   vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite);

  // Clear the color attachment image to black.
  cb->Blit(black_, {0, 0}, {1, 1}, color_attachment_, {0, 0},
           {kFramebufferWidth, kFramebufferHeight}, vk::Filter::eNearest);

  // Wait for the image to be cleared to black before processing any other
  // commands (we conservatively use vk::PipelineStageFlagBits::eAllCommands
  // because we don't know for sure what the client will do).  Afterward,
  // the image layout is whatever the client requested.
  cb->ImageBarrier(color_attachment_, vk::ImageLayout::eTransferDstOptimal, framebuffer_layout,
                   vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite,
                   vk::PipelineStageFlagBits::eAllCommands,
                   vk::AccessFlagBits::eTransferWrite | vk::AccessFlagBits::eTransferRead |
                       vk::AccessFlagBits::eColorAttachmentWrite);

  // This allows the client to use the image as part of a FrameBuffer so that there is not an
  // undefined layout.
  color_attachment_->set_swapchain_layout(framebuffer_layout);

  return FrameData{
      .frame = frame, .color_attachment = color_attachment_, .depth_attachment = depth_attachment_};
}

std::vector<uint8_t> ReadbackTest::ReadbackFromColorAttachment(const FramePtr& frame,
                                                               vk::ImageLayout current_image_layout,
                                                               vk::ImageLayout final_image_layout) {
  CommandBuffer* cb = frame->cmds();

  cb->KeepAlive(readback_buffer_);
  cb->KeepAlive(color_attachment_);

  // Allow previous cmds to finish modifying the color attachment.  Also,
  // transition to eTransferSrcOptimal before copying the bytes.
  cb->ImageBarrier(color_attachment_, current_image_layout, vk::ImageLayout::eTransferSrcOptimal,
                   vk::PipelineStageFlagBits::eAllCommands,
                   vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eTransferWrite,
                   vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead);

  // Read back the data.
  vk::BufferImageCopy readback_cmd;
  readback_cmd.bufferRowLength = kFramebufferWidth;
  readback_cmd.bufferImageHeight = kFramebufferHeight;
  readback_cmd.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
  readback_cmd.imageSubresource.layerCount = 1;
  readback_cmd.imageExtent.width = kFramebufferWidth;
  readback_cmd.imageExtent.height = kFramebufferHeight;
  readback_cmd.imageExtent.depth = 1;
  cb->vk().copyImageToBuffer(color_attachment_->vk(), vk::ImageLayout::eTransferSrcOptimal,
                             readback_buffer_->vk(), 1, &readback_cmd);

  // Since we call waitIdle() below, this is not about synchronization, only
  // changing to the image layout requested by the caller.
  cb->ImageBarrier(color_attachment_, vk::ImageLayout::eTransferSrcOptimal, final_image_layout,
                   vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead,
                   vk::PipelineStageFlagBits::eAllCommands,
                   vk::AccessFlagBits::eTransferWrite | vk::AccessFlagBits::eTransferRead |
                       vk::AccessFlagBits::eColorAttachmentWrite);

  // Submit the commands, wait for them to finish, and then copy and return the
  // data to the caller.
  frame->SubmitPartialFrame(SemaphorePtr());
  escher_->vk_device().waitIdle();

  std::vector<uint8_t> result;
  result.resize(kNumFramebufferBytes);
  memcpy(result.data(), readback_buffer_->host_ptr(), kNumFramebufferBytes);
  return result;
}

}  // namespace escher
