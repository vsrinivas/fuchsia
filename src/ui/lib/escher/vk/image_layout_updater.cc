// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/image_layout_updater.h"

#include <utility>

#include "src/ui/lib/escher/vk/command_buffer.h"

namespace escher {

namespace {

// For given new layout, we need to specify the pipeline stage and the access
// flags for that image in vkCmdPipelineBarrier. We use the following logic to
// get destination stage and access mask flags for each given image layout.
std::pair<vk::PipelineStageFlags, vk::AccessFlags> GetDstMask(vk::ImageLayout new_layout) {
  vk::PipelineStageFlags dst_stage_mask;
  vk::AccessFlags dst_access_mask;
  switch (new_layout) {
    case vk::ImageLayout::eColorAttachmentOptimal:
      dst_access_mask =
          vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
      dst_stage_mask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
      break;
    case vk::ImageLayout::eDepthStencilAttachmentOptimal:
      dst_access_mask = vk::AccessFlagBits::eDepthStencilAttachmentRead |
                        vk::AccessFlagBits::eDepthStencilAttachmentWrite;
      dst_stage_mask = vk::PipelineStageFlagBits::eEarlyFragmentTests;
      break;
    case vk::ImageLayout::eGeneral:
      dst_access_mask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
      dst_stage_mask = vk::PipelineStageFlagBits::eComputeShader;
      break;
    case vk::ImageLayout::ePresentSrcKHR:
      dst_access_mask = vk::AccessFlagBits::eMemoryRead;
      dst_stage_mask = vk::PipelineStageFlagBits::eAllGraphics;
      break;
    case vk::ImageLayout::eShaderReadOnlyOptimal:
      dst_access_mask = vk::AccessFlagBits::eShaderRead;
      dst_stage_mask = vk::PipelineStageFlagBits::eAllCommands;
      break;
    case vk::ImageLayout::eTransferDstOptimal:
      dst_access_mask = vk::AccessFlagBits::eTransferWrite;
      dst_stage_mask = vk::PipelineStageFlagBits::eTransfer;
      break;
    case vk::ImageLayout::eTransferSrcOptimal:
      dst_access_mask = vk::AccessFlagBits::eTransferRead;
      dst_stage_mask = vk::PipelineStageFlagBits::eTransfer;
      break;
    case vk::ImageLayout::eUndefined:
    default:
      FXL_LOG(ERROR) << "CommandBuffer does not know how to transition to layout: "
                     << vk::to_string(new_layout);
      FXL_DCHECK(false);
  }
  return std::make_pair(dst_stage_mask, dst_access_mask);
}

}  // namespace

ImageLayoutUpdater::ImageLayoutUpdater(EscherWeakPtr const escher) : escher_(escher) {
  if (!escher_) {
    FXL_LOG(ERROR) << "Fatal: No valid escher, ImageLayoutUpdater will fail.";
  }
}

ImageLayoutUpdater::~ImageLayoutUpdater() {
  // Check that there is no pending tasks / pending semaphores.
  FXL_DCHECK(images_to_set_.empty() && pending_image_layout_to_set_.empty() &&
             wait_semaphores_.empty() && signal_semaphores_.empty());
}

void ImageLayoutUpdater::ScheduleSetImageInitialLayout(const escher::ImagePtr& image,
                                                       vk::ImageLayout new_layout) {
  FXL_DCHECK(image->layout() == vk::ImageLayout::eUndefined ||
             image->layout() == vk::ImageLayout::ePreinitialized);
  FXL_CHECK(images_to_set_.find(image) == images_to_set_.end())
      << "Initial layout can be set only once for each image.";
  images_to_set_.insert(image);
  pending_image_layout_to_set_.emplace_back(image, new_layout);
}

void ImageLayoutUpdater::GenerateCommands(CommandBuffer* cmds) {
  if (!NeedsCommandBuffer()) {
    return;
  }

  // Check existence of the command buffer |cmds|.
  FXL_DCHECK(cmds);
  for (const auto& [image, new_layout] : pending_image_layout_to_set_) {
    FXL_DCHECK(!image->is_layout_initialized())
        << "Error: layout of VkImage " << image->vk() << " is already initialized.";
    auto src_stage_mask = vk::PipelineStageFlagBits::eTopOfPipe;
    auto src_access_mask = vk::AccessFlags();
    std::pair<vk::PipelineStageFlags, vk::AccessFlags> dst_masks;
    if (cmds->type() == CommandBuffer::Type::kTransfer) {
      // For transfer command buffers, since there can be only transfer
      // commands, we will only synchronize image layout update with transfer
      // pipeline stage.
      dst_masks = {vk::PipelineStageFlagBits::eTransfer,
                   vk::AccessFlagBits::eTransferWrite | vk::AccessFlagBits::eTransferRead};
    } else {
      // For command buffers of other type (compute, graphics), we choose
      // the destination masks based on its target layout.
      dst_masks = GetDstMask(new_layout);
    }
    auto dst_stage_mask = dst_masks.first;
    auto dst_access_mask = dst_masks.second;
    cmds->ImageBarrier(image, image->layout(), new_layout, src_stage_mask, src_access_mask,
                       dst_stage_mask, dst_access_mask);
    cmds->KeepAlive(image);
  }

  // Add semaphores for the submitted command buffer to wait on.
  for (auto& pair : wait_semaphores_) {
    cmds->AddWaitSemaphore(std::move(pair.first), pair.second);
  }
  wait_semaphores_.clear();

  // Add semaphores for the submitted command buffer to signal.
  for (auto& sem : signal_semaphores_) {
    cmds->AddSignalSemaphore(std::move(sem));
  }
  signal_semaphores_.clear();

  images_to_set_.clear();
  pending_image_layout_to_set_.clear();
}

void ImageLayoutUpdater::Submit(fit::function<void()> callback, CommandBuffer::Type type) {
  if (NeedsCommandBuffer()) {
    auto cmds = CommandBuffer::NewForType(escher_.get(), type, /* use_protected_memory */ false);
    GenerateCommands(cmds.get());
    cmds->Submit(std::move(callback));
  }

  // After this function is called, the |pending_image_layout_to_set_|,
  // |images_to_set_| and all semaphores will be clear so that the image layout
  // updater will be reused again.
  FXL_DCHECK(images_to_set_.empty() && pending_image_layout_to_set_.empty() &&
             wait_semaphores_.empty() && signal_semaphores_.empty());
}

}  // namespace escher
