// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/screenshotter.h"

#include <lib/trace/event.h>
#include <lib/zx/time.h>

#include <functional>
#include <utility>
#include <vector>

#include "src/lib/files/file.h"
#include "src/lib/fsl/vmo/sized_vmo.h"
#include "src/lib/fsl/vmo/vector.h"
#include "src/ui/lib/escher/impl/command_buffer.h"
#include "src/ui/lib/escher/impl/command_buffer_pool.h"
#include "src/ui/lib/escher/impl/image_cache.h"
#include "src/ui/lib/escher/vk/image_layout_updater.h"
#include "src/ui/scenic/lib/gfx/engine/engine_renderer.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/compositor.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer.h"
#include "src/ui/scenic/lib/gfx/util/time.h"

namespace scenic_impl {
namespace gfx {

namespace {

// HACK(fxbug.dev/24454): The FIDL requires a valid VMO (even in failure cases).
fuchsia::ui::scenic::ScreenshotData EmptyScreenshot() {
  fuchsia::ui::scenic::ScreenshotData screenshot;
  // TODO(fxbug.dev/24454): If we can't create an empty VMO, bail because otherwise the
  // caller will hang indefinitely.
  FX_CHECK(zx::vmo::create(0, 0u, &screenshot.data.vmo) == ZX_OK);
  return screenshot;
}

// Function to rotate the array of pixel data depending on the rotation
// input given. Values should be multiples of 90. Returns a vector of
// the rotated pixels. The width and height are passed by reference and
// updated to reflect the new orientation in the event of rotation by
// 90 or 270 degrees. All rotation is counterclockwise.
//
// This may potentially cause some unnecessary bottlenecking since
// Scenic is currently single-threaded. In the future we might want to
// move this to the root presenter, which runs on a separate process,
// or when Scenic eventually becomes multi-threaded, we keep it here and
// and run the rotation on a background thread.
std::vector<uint8_t> rotate_img_vec(const std::vector<uint8_t>& imgvec, uint32_t& width,
                                    uint32_t& height, uint32_t bytes_per_pixel, uint32_t rotation) {
  // Trace performance.
  TRACE_DURATION("gfx", "Screenshotter rotate_img_vec");

  // Rotation should always be a multiple of 90 degrees, and not 0.
  rotation = rotation % 360;
  FX_CHECK(rotation % 90 == 0 && rotation != 0);

  // Rotation determines which of the width and height
  // are the inner and outer loop.
  uint32_t outer = (rotation == 180) ? height : width;
  uint32_t inner = (rotation == 180) ? width : height;

  // Code for rotation of 90 degrees, 180 or 270 degrees.
  std::vector<uint8_t> result;
  result.reserve(width * height * bytes_per_pixel);

  for (uint32_t i = 0; i < outer; i++) {
    for (uint32_t j = 0; j < inner; j++) {
      uint32_t x;
      uint32_t y;
      // Because of the order pixels are appended, |j| is the x axis of the new
      // vector and |i| is the y axis.
      switch (rotation) {
        case 90:
          x = width - i - 1;
          y = j;
          break;

        case 180:
          // x and y depend on different variables in this case.
          x = width - j - 1;
          y = height - i - 1;
          break;

        case 270:
          x = i;
          y = height - j - 1;
          break;
      };

      for (uint32_t b = 0; b < bytes_per_pixel; b++) {
        result.push_back(imgvec[(x + y * width) * bytes_per_pixel + b]);
      }
    }
  }

  // Must reverse width and height of image.
  if (rotation == 90 || rotation == 270) {
    std::swap(width, height);
  }
  return result;
}

// If this changes, or if we must determine this dynamically, look for other places
// that the same constant is used to see if they must also be changed.
constexpr vk::Format kScenicScreenshotFormat = vk::Format::eB8G8R8A8Srgb;

constexpr uint32_t kBytesPerPixel = 4u;

};  // namespace

// static
void Screenshotter::OnCommandBufferDone(
    const escher::BufferPtr& buffer, uint32_t width, uint32_t height, uint32_t rotation,
    fuchsia::ui::scenic::Scenic::TakeScreenshotCallback done_callback) {
  TRACE_DURATION("gfx", "Screenshotter::OnCommandBufferDone");

  std::vector<uint8_t> imgvec;
  const size_t kImgVecElementSize = sizeof(decltype(imgvec)::value_type);
  imgvec.resize(kBytesPerPixel * width * height);

  const uint8_t* row = buffer->host_ptr();
  FX_CHECK(row != nullptr);
  uint32_t num_bytes = width * height * kBytesPerPixel;
  FX_DCHECK(num_bytes <= kImgVecElementSize * imgvec.size());
  memcpy(imgvec.data(), row, num_bytes);

  // Apply rotation of 90, 180 or 270 degrees counterclockwise.
  if (rotation % 360 != 0) {
    imgvec = rotate_img_vec(imgvec, width, height, kBytesPerPixel, rotation);
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
    Engine* engine, fuchsia::ui::scenic::Scenic::TakeScreenshotCallback done_callback) {
  TRACE_DURATION("gfx", "Screenshotter::TakeScreenshot");
  auto* escher = engine->escher();
  const CompositorWeakPtr& compositor = engine->scene_graph()->first_compositor();

  if (!compositor || compositor->GetNumDrawableLayers() == 0) {
    FX_LOGS(WARNING) << "TakeScreenshot: No drawable layers; returning empty screenshot.";
    done_callback(EmptyScreenshot(), false);
    return;
  }
  uint32_t width;
  uint32_t height;
  std::tie(width, height) = compositor->GetBottomLayerSize();

  uint32_t rotation = compositor->layout_rotation();
  escher::ImageInfo image_info;
  image_info.format = kScenicScreenshotFormat;
  image_info.width = width;
  image_info.height = height;
  image_info.usage = vk::ImageUsageFlagBits::eColorAttachment |
                     vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;

  // TODO(fxbug.dev/23725): cache is never trimmed.
  escher::ImagePtr image = escher->image_cache()->NewImage(image_info);
  escher::FramePtr frame = escher->NewFrame("Scenic Compositor", 0);

  // Transition layout of |image| to |eColorAttachmentOptimal|.
  image->set_swapchain_layout(vk::ImageLayout::eColorAttachmentOptimal);
  frame->cmds()->ImageBarrier(
      image, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
      vk::PipelineStageFlagBits::eTopOfPipe, vk::AccessFlags(),
      vk::PipelineStageFlagBits::eColorAttachmentOutput,
      vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eColorAttachmentRead);

  std::vector<Layer*> drawable_layers = compositor->GetDrawableLayers();
  engine->renderer()->RenderLayers(frame, zx::time(dispatcher_clock_now()), {.output_image = image},
                                   drawable_layers);

  // Generate Vulkan Semaphore pairs so that gfx tasks such as screenshotting,
  // rendering, etc. are properly synchronized.
  // See the class comment of |Engine| for details.
  auto semaphore_pair = escher->semaphore_chain()->TakeLastAndCreateNextSemaphore();
  frame->cmds()->AddSignalSemaphore(std::move(semaphore_pair.semaphore_to_signal));
  frame->cmds()->AddWaitSemaphore(
      std::move(semaphore_pair.semaphore_to_wait),
      vk::PipelineStageFlagBits::eVertexInput | vk::PipelineStageFlagBits::eFragmentShader |
          vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eTransfer);

  // TODO(fxbug.dev/24304): Nobody signals this semaphore, so there's no point.  One
  // way that it could be used is export it as a zx::event and watch for that to
  // be signaled instead of adding a completion-callback to the command-buffer.
  auto frame_done_semaphore = escher::Semaphore::New(escher->vk_device());
  frame->EndFrame(frame_done_semaphore, nullptr);

  // TODO(fxbug.dev/24304): instead of submitting another command buffer, this could be
  // done as part of the same Frame above.

  vk::Queue queue = escher->command_buffer_pool()->queue();
  auto* command_buffer = escher->command_buffer_pool()->GetCommandBuffer();

  escher::BufferPtr buffer = escher->buffer_cache()->NewHostBuffer(width * height * kBytesPerPixel);

  vk::BufferImageCopy region;
  region.bufferRowLength = width;
  region.bufferImageHeight = height;
  region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
  region.imageSubresource.layerCount = 1;
  region.imageExtent.width = width;
  region.imageExtent.height = height;
  region.imageExtent.depth = 1;
  command_buffer->TransitionImageLayout(image, vk::ImageLayout::eColorAttachmentOptimal,
                                        vk::ImageLayout::eTransferSrcOptimal);
  command_buffer->vk().copyImageToBuffer(image->vk(), vk::ImageLayout::eTransferSrcOptimal,
                                         buffer->vk(), 1, &region);
  command_buffer->KeepAlive(image);

  command_buffer->Submit(
      queue, [buffer, width, height, rotation, done_callback = std::move(done_callback)]() mutable {
        OnCommandBufferDone(buffer, width, height, rotation, std::move(done_callback));
      });

  // Force the command buffer to retire to guarantee that |done_callback| will
  // be called in a timely fashion.
  engine->CleanupEscher();
}

}  // namespace gfx
}  // namespace scenic_impl
