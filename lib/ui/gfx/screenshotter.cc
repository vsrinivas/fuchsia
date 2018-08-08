// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/screenshotter.h"

#include <functional>
#include <utility>
#include <vector>

#include <lib/zx/time.h>

#include "garnet/lib/ui/gfx/resources/compositor/compositor.h"
#include "garnet/lib/ui/gfx/resources/compositor/layer.h"
#include "garnet/lib/ui/scenic/scenic.h"
#include "lib/escher/impl/command_buffer.h"
#include "lib/escher/impl/command_buffer_pool.h"
#include "lib/escher/impl/image_cache.h"
#include "lib/fsl/vmo/sized_vmo.h"
#include "lib/fsl/vmo/vector.h"
#include "lib/fxl/functional/make_copyable.h"

namespace scenic {
namespace gfx {

// static
void Screenshotter::OnCommandBufferDone(
    const escher::ImagePtr& image, uint32_t width, uint32_t height,
    vk::Device device,
    fuchsia::ui::scenic::Scenic::TakeScreenshotCallback done_callback) {
  // Map the final image so CPU can read it.
  const vk::ImageSubresource sr(vk::ImageAspectFlagBits::eColor, 0, 0);
  vk::SubresourceLayout sr_layout;
  device.getImageSubresourceLayout(image->vk(), &sr, &sr_layout);

  constexpr uint32_t kBytesPerPixel = 4u;
  std::vector<uint8_t> imgvec;
  const size_t kImgVecElementSize = sizeof(decltype(imgvec)::value_type);
  imgvec.resize(kBytesPerPixel * width * height);

  const uint8_t* row = image->memory()->mapped_ptr();
  FXL_CHECK(row != nullptr);
  row += sr_layout.offset;
  if (width == sr_layout.rowPitch) {
    uint32_t num_bytes = width * height * kBytesPerPixel;
    FXL_DCHECK(num_bytes <= kImgVecElementSize * imgvec.size());
    memcpy(imgvec.data(), row, num_bytes);
  } else {
    uint8_t* imgvec_ptr = imgvec.data();
    for (uint32_t y = 0; y < height; y++) {
      uint32_t num_bytes = width * kBytesPerPixel;
      FXL_DCHECK(num_bytes <=
                 kImgVecElementSize * (1 + &imgvec.back() - imgvec_ptr));
      memcpy(imgvec_ptr, row, num_bytes);
      row += sr_layout.rowPitch;
      imgvec_ptr += num_bytes;
    }
  }

  fsl::SizedVmo sized_vmo;
  if (!fsl::VmoFromVector(imgvec, &sized_vmo)) {
    done_callback(fuchsia::ui::scenic::ScreenshotData{}, false);
  }

  fuchsia::ui::scenic::ScreenshotData data;
  data.data = std::move(sized_vmo).ToTransport();
  data.info.width = width;
  data.info.height = height;
  data.info.stride = width * kBytesPerPixel;
  done_callback(std::move(data), true);
}

void Screenshotter::TakeScreenshot(
    fuchsia::ui::scenic::Scenic::TakeScreenshotCallback done_callback) {
  auto* escher = engine_->escher();
  Compositor* compositor = engine_->GetFirstCompositor();

  if (compositor->GetNumDrawableLayers() == 0) {
    FXL_LOG(ERROR) << "No drawable layers.";
    done_callback(fuchsia::ui::scenic::ScreenshotData{}, false);
    return;
  }
  uint32_t width;
  uint32_t height;
  std::tie(width, height) = compositor->GetBottomLayerSize();
  escher::ImageInfo image_info;
  image_info.format = vk::Format::eB8G8R8A8Unorm;
  image_info.width = width;
  image_info.height = height;
  image_info.usage = vk::ImageUsageFlagBits::eColorAttachment |
                     vk::ImageUsageFlagBits::eSampled;
  image_info.memory_flags = vk::MemoryPropertyFlagBits::eHostVisible;
  image_info.tiling = vk::ImageTiling::eLinear;

  escher::ImagePtr image = escher->image_cache()->NewImage(image_info);
  auto frame_done_semaphore = escher::Semaphore::New(escher->vk_device());
  compositor->DrawToImage(engine_->paper_renderer(), engine_->shadow_renderer(),
                          image, frame_done_semaphore);

  vk::Queue queue = escher->command_buffer_pool()->queue();
  auto* command_buffer = escher->command_buffer_pool()->GetCommandBuffer();

  command_buffer->Submit(
      queue,
      fxl::MakeCopyable([image, width, height, device = escher->vk_device(),
                         done_callback = std::move(done_callback)]() mutable {
        OnCommandBufferDone(image, width, height, device,
                            std::move(done_callback));
      }));
  // Force the command buffer to retire so that the submitted commands will run.
  // TODO(SCN-211): Make this a proper wait instead of spinning.
  while (!escher->command_buffer_pool()->Cleanup()) {
    zx::nanosleep(zx::deadline_after(zx::sec(1)));
  }
}

}  // namespace gfx
}  // namespace scenic
