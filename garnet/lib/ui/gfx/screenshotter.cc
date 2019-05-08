// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/screenshotter.h"

#include <functional>
#include <utility>
#include <vector>

#include <lib/zx/time.h>

#include "garnet/lib/ui/gfx/engine/engine_renderer.h"
#include "garnet/lib/ui/gfx/resources/compositor/compositor.h"
#include "garnet/lib/ui/gfx/resources/compositor/layer.h"
#include "garnet/lib/ui/gfx/util/time.h"
#include "garnet/lib/ui/scenic/scenic.h"
#include "src/ui/lib/escher/impl/command_buffer.h"
#include "src/ui/lib/escher/impl/command_buffer_pool.h"
#include "src/ui/lib/escher/impl/image_cache.h"
#include "lib/fsl/vmo/sized_vmo.h"
#include "lib/fsl/vmo/vector.h"

namespace scenic_impl {
namespace gfx {

namespace {

// HACK(SCN-1253): The FIDL requires a valid VMO (even in failure cases).
fuchsia::ui::scenic::ScreenshotData EmptyScreenshot() {
  fuchsia::ui::scenic::ScreenshotData screenshot;
  // TODO(SCN-1253): If we can't create an empty VMO, bail because otherwise the
  // caller will hang indefinitely.
  FXL_CHECK(zx::vmo::create(0, 0u, &screenshot.data.vmo) == ZX_OK);
  return screenshot;
}

};  // namespace

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

  const uint8_t* row = image->host_ptr();
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
    done_callback(EmptyScreenshot(), false);
  }

  fuchsia::ui::scenic::ScreenshotData data;
  data.data = std::move(sized_vmo).ToTransport();
  data.info.width = width;
  data.info.height = height;
  data.info.stride = width * kBytesPerPixel;
  done_callback(std::move(data), true);
}

void Screenshotter::TakeScreenshot(
    Engine* engine,
    fuchsia::ui::scenic::Scenic::TakeScreenshotCallback done_callback) {
  auto* escher = engine->escher();
  const CompositorWeakPtr& compositor =
      engine->scene_graph()->first_compositor();

  if (!compositor || compositor->GetNumDrawableLayers() == 0) {
    FXL_LOG(WARNING)
        << "TakeScreenshot: No drawable layers; returning empty screenshot.";
    done_callback(EmptyScreenshot(), false);
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

  // TODO(ES-7): cache is never trimmed.
  escher::ImagePtr image = escher->image_cache()->NewImage(image_info);
  escher::FramePtr frame = escher->NewFrame("Scenic Compositor", 0);

  std::vector<Layer*> drawable_layers = compositor->GetDrawableLayers();
  engine->renderer()->RenderLayers(frame, dispatcher_clock_now(), image,
                                   drawable_layers);

  // TODO(SCN-1096): Nobody signals this semaphore, so there's no point.  One
  // way that it could be used is export it as a zx::event and watch for that to
  // be signaled instead of adding a completion-callback to the command-buffer.
  auto frame_done_semaphore = escher::Semaphore::New(escher->vk_device());
  frame->EndFrame(frame_done_semaphore, nullptr);

  // TODO(SCN-1096): instead of submitting another command buffer, this could be
  // done as part of the same Frame above.
  vk::Queue queue = escher->command_buffer_pool()->queue();
  auto* command_buffer = escher->command_buffer_pool()->GetCommandBuffer();

  command_buffer->Submit(queue,
                         [image, width, height, device = escher->vk_device(),
                          done_callback = std::move(done_callback)]() mutable {
                           OnCommandBufferDone(image, width, height, device,
                                               std::move(done_callback));
                         });

  // Force the command buffer to retire to guarantee that |done_callback| will
  // be called in a timely fashion.
  engine->CleanupEscher();
}

}  // namespace gfx
}  // namespace scenic_impl
