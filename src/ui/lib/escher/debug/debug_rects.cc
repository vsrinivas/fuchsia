// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/debug/debug_rects.h"

#include "src/lib/fxl/logging.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/types/color.h"
#include "src/ui/lib/escher/util/alloca.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/vk/image.h"

namespace escher {

std::unique_ptr<DebugRects> DebugRects::New(BatchGpuUploader* uploader, ImageFactory* factory) {
  auto image = image_utils::NewRgbaImage(factory, uploader, kMax, 1, colorData[0].bytes(),
                                         vk::ImageLayout::eTransferSrcOptimal);
  // bytes parameter requires access to array through first index
  return std::unique_ptr<DebugRects>(new DebugRects(std::move(image)));
}

DebugRects::DebugRects(ImagePtr image) : palette_(std::move(image)) { FXL_DCHECK(palette_); }

void DebugRects::Blit(CommandBuffer* cb, Color color, const ImagePtr& target, vk::Rect2D rect) {
  cb->impl()->TakeWaitSemaphore(palette_, vk::PipelineStageFlagBits::eTransfer);
  cb->impl()->TakeWaitSemaphore(target, vk::PipelineStageFlagBits::eColorAttachmentOutput |
                                            vk::PipelineStageFlagBits::eTransfer);
  cb->impl()->KeepAlive(target);

  vk::ImageBlit region;
  uint32_t region_count = 1;

  region.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
  region.srcSubresource.mipLevel = 0;
  region.srcSubresource.baseArrayLayer = 0;
  region.srcSubresource.layerCount = 1;
  region.dstSubresource = region.srcSubresource;
  region.srcOffsets[0] = vk::Offset3D(color, 0, 0);
  region.srcOffsets[1] = vk::Offset3D(color + 1, 1, 1);
  region.dstOffsets[0] = vk::Offset3D(rect.offset.x, rect.offset.y, 0);
  region.dstOffsets[1] = vk::Offset3D(rect.extent.width, rect.extent.height, 1);

  cb->vk().blitImage(palette_->vk(), vk::ImageLayout::eTransferSrcOptimal, target->vk(),
                     vk::ImageLayout::eTransferDstOptimal, region_count, &region,
                     vk::Filter::eNearest);
}

}  // namespace escher
