#include "garnet/lib/ui/gfx/screenshotter.h"

#include <fstream>
#include <functional>

#include "garnet/lib/ui/gfx/resources/compositor/compositor.h"
#include "garnet/lib/ui/gfx/resources/compositor/layer.h"
#include "garnet/lib/ui/scenic/scenic.h"
#include "lib/escher/impl/command_buffer.h"
#include "lib/escher/impl/command_buffer_pool.h"
#include "lib/escher/impl/image_cache.h"
#include "lib/fxl/synchronization/sleep.h"

namespace scenic {
namespace gfx {

// static
void Screenshotter::OnCommandBufferDone(
    const std::string& filename,
    const escher::ImagePtr& image,
    uint32_t width,
    uint32_t height,
    vk::Device device,
    ui::Scenic::TakeScreenshotCallback done_callback) {
  // Map the final image so CPU can read it.
  const vk::ImageSubresource sr(vk::ImageAspectFlagBits::eColor, 0, 0);
  vk::SubresourceLayout sr_layout;
  device.getImageSubresourceLayout(image->vk(), &sr, &sr_layout);
  // Write the data to a PPM file.
  std::ofstream file(filename, std::ios::binary);
  if (!file.is_open()) {
    FXL_LOG(ERROR) << "Could not open screenshot file: " << filename;
    done_callback(false);
    return;
  }

  file << "P6\n";
  file << width << "\n";
  file << height << "\n";
  file << 255 << "\n";

  // Image has 4 channels, but we don't write the alpha channel.
  const uint8_t* row = image->memory()->mapped_ptr();
  FXL_CHECK(row != nullptr);
  row += sr_layout.offset;
  for (uint32_t y = 0; y < height; y++) {
    const unsigned int* pixel = (const unsigned int*) row;
    for (uint32_t x = 0; x < width; x++) {
      file.write((char*) pixel, 3);
      ++pixel;
    }
    row += sr_layout.rowPitch;
  }
  file.close();
  done_callback(true);
}

void Screenshotter::TakeScreenshot(
    const std::string& filename,
    ui::Scenic::TakeScreenshotCallback done_callback) {
  auto* escher = engine_->escher();
  Compositor* compositor = engine_->GetFirstCompositor();

  if (compositor->GetNumDrawableLayers() == 0) {
    FXL_LOG(ERROR) << "No drawable layers.";
    done_callback(false);
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

  escher::ImagePtr image =
      escher->image_cache()->NewImage(image_info);
  auto frame_done_semaphore = escher::Semaphore::New(escher->vk_device());
  compositor->DrawToImage(
      engine_->paper_renderer(), engine_->shadow_renderer(),
      image, frame_done_semaphore);

  vk::Queue queue = escher->command_buffer_pool()->queue();
  auto* command_buffer = escher->command_buffer_pool()->GetCommandBuffer();

  auto submit_callback = std::bind(
      &OnCommandBufferDone, filename, image, width, height, escher->vk_device(),
      done_callback);
  command_buffer->Submit(queue, std::move(submit_callback));
  // Force the command buffer to retire so that the submitted commands will run.
  // TODO(SCN-211): Make this a proper wait instead of spinning.
  while (!escher->command_buffer_pool()->Cleanup()) {
    fxl::SleepFor(fxl::TimeDelta::FromSeconds(1));
  }
}

}  // namespace gfx
}  // namespace scenic
