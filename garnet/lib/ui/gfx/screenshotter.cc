// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/screenshotter.h"

#include <lib/zx/time.h>
#include <trace/event.h>

#include <functional>
#include <utility>
#include <vector>

#include "garnet/lib/ui/gfx/engine/engine_renderer.h"
#include "garnet/lib/ui/gfx/resources/compositor/compositor.h"
#include "garnet/lib/ui/gfx/resources/compositor/layer.h"
#include "garnet/lib/ui/gfx/util/time.h"
#include "garnet/lib/ui/scenic/scenic.h"
#include "lib/fsl/vmo/sized_vmo.h"
#include "lib/fsl/vmo/vector.h"
#include "src/lib/files/file.h"
#include "src/ui/lib/escher/impl/command_buffer.h"
#include "src/ui/lib/escher/impl/command_buffer_pool.h"
#include "src/ui/lib/escher/impl/image_cache.h"

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
std::vector<uint8_t> rotate_img_vec(const std::vector<uint8_t>& imgvec,
                                    uint32_t& width, uint32_t& height,
                                    uint32_t bytes_per_pixel,
                                    uint32_t rotation) {
  // Trace performance.
  TRACE_DURATION("gfx", "Screenshotter rotate_img_vec");

  // Rotation should always be a multiple of 90 degrees.
  FXL_CHECK(rotation % 90 == 0 && rotation < 360);

  // Rotation determines which of the width and height
  // are the inner and outer loop.
  uint32_t outer = (rotation == 180) ? height : width;
  uint32_t inner = (rotation == 180) ? width : height;

  // Code for rotation of 90 degrees, 180 or 270 degrees.
  std::vector<uint8_t> result;
  result.reserve(width * height * bytes_per_pixel);

  for (uint32_t i = 0; i < outer; i++) {
    for (uint32_t j = 0; j < inner; j++) {
      // Determine which loop represents x or y.
      uint32_t x = (rotation == 180) ? j : i;
      uint32_t y = (rotation == 180) ? i : j;

      // Take inverse y for 180 or 270 degrees.
      uint32_t new_y = (rotation == 90) ? y : (height - y - 1);

      for (uint32_t b = 0; b < bytes_per_pixel; b++) {
        result.push_back(imgvec[(x + new_y * width) * bytes_per_pixel + b]);
      }
    }
  }

  // Must reverse width and height of image.
  if (rotation == 90 || rotation == 270) {
    std::swap(width, height);
  }
  return result;
}

};  // namespace

// static
void Screenshotter::OnCommandBufferDone(
    const escher::ImagePtr& image, uint32_t width, uint32_t height,
    uint32_t rotation, vk::Device device,
    fuchsia::ui::scenic::Scenic::TakeScreenshotCallback done_callback) {
  TRACE_DURATION("gfx", "Screenshotter::OnCommandBufferDone");
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

  // Apply rotation of 90, 180 or 270 degrees counterclockwise.
  if (rotation > 0) {
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

  uint32_t rotation = compositor->layout_rotation();
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

  command_buffer->Submit(
      queue, [image, width, height, rotation, device = escher->vk_device(),
              done_callback = std::move(done_callback)]() mutable {
        OnCommandBufferDone(image, width, height, rotation, device,
                            std::move(done_callback));
      });

  // Force the command buffer to retire to guarantee that |done_callback| will
  // be called in a timely fashion.
  engine->CleanupEscher();
}

}  // namespace gfx
}  // namespace scenic_impl
