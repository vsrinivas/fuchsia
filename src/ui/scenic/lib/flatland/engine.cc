// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/engine.h"

#include <lib/fdio/directory.h>
#include <zircon/pixelformat.h>

#include <vector>

#include "src/ui/scenic/lib/flatland/global_image_data.h"

namespace flatland {

namespace {

// Struct to combine the source and destination frames used to set a layer's
// position on the display. The src frame represents the (cropped) UV coordinates
// of the image and the dst frame represents the position in screen space that
// the layer will be placed.
struct DisplayFrameData {
  fuchsia::hardware::display::Frame src;
  fuchsia::hardware::display::Frame dst;
};

constexpr fuchsia::sysmem::BufferUsage kNoneUsage = {.none = fuchsia::sysmem::noneUsage};

uint64_t InitializeDisplayLayer(fuchsia::hardware::display::ControllerSyncPtr& display_controller) {
  uint64_t layer_id;
  zx_status_t create_layer_status;
  zx_status_t transport_status = display_controller->CreateLayer(&create_layer_status, &layer_id);
  if (create_layer_status != ZX_OK || transport_status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create layer, " << create_layer_status;
    return 0;
  }
  return layer_id;
}

std::vector<uint64_t> CreateAndSetDisplayLayers(
    fuchsia::hardware::display::ControllerSyncPtr& display_controller, uint64_t display_id,
    uint64_t num_layers) {
  std::vector<uint64_t> layers;
  for (uint32_t i = 0; i < num_layers; i++) {
    auto curr_layer_id = InitializeDisplayLayer(display_controller);
    layers.push_back(curr_layer_id);
  }

  // Set all of the layers for each of the images on the display.
  auto status = display_controller->SetDisplayLayers(display_id, layers);
  FX_DCHECK(status == ZX_OK);

  return layers;
}

// Imports an image to the display controller and returns an ID by which that image can
// be referenced by the display.
uint64_t ImportImage(fuchsia::hardware::display::ControllerSyncPtr& display_controller,
                     ImageMetadata image) {
  uint64_t image_id;
  fuchsia::hardware::display::ImageConfig image_config = {.width = image.width,
                                                          .height = image.height};
  zx_status_t import_image_status = ZX_OK;
  display_controller->ImportImage(image_config, image.collection_id, image.vmo_idx,
                                  &import_image_status, &image_id);
  FX_DCHECK(import_image_status == ZX_OK);

  return image_id;
}

// When setting an image on a layer in the display, you have to specify the "source"
// and "destination", where the source represents the pixel offsets and dimensions to
// use from the image and the destination represents where on the display the (cropped)
// image will go in pixel coordinates. This exactly mirrors the setup we have in the
// Rectangle2D struct and ImageMetadata struct, so we just need to convert that over to
// the proper display controller readable format.
DisplayFrameData RectangleDataToDisplayFrames(escher::Rectangle2D rectangle, ImageMetadata image) {
  fuchsia::hardware::display::Frame src_frame = {
      .x_pos = static_cast<uint32_t>(rectangle.clockwise_uvs[0].x * image.width),
      .y_pos = static_cast<uint32_t>(rectangle.clockwise_uvs[0].y * image.height),
      .width = static_cast<uint32_t>((rectangle.clockwise_uvs[2].x - rectangle.clockwise_uvs[0].x) *
                                     image.width),
      .height = static_cast<uint32_t>(
          (rectangle.clockwise_uvs[2].y - rectangle.clockwise_uvs[0].y) * image.height),
  };

  fuchsia::hardware::display::Frame dst_frame = {
      .x_pos = static_cast<uint32_t>(rectangle.origin.x),
      .y_pos = static_cast<uint32_t>(rectangle.origin.y),
      .width = static_cast<uint32_t>(rectangle.extent.x),
      .height = static_cast<uint32_t>(rectangle.extent.y),
  };
  return {.src = src_frame, .dst = dst_frame};
}

}  // namespace

Engine::Engine(
    const std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr>& display_controller,
    const std::shared_ptr<LinkSystem>& link_system,
    const std::shared_ptr<UberStructSystem>& uber_struct_system)
    : display_controller_(display_controller),
      link_system_(link_system),
      uber_struct_system_(uber_struct_system) {}

std::vector<Engine::RenderData> Engine::ComputeRenderData() {
  const auto snapshot = uber_struct_system_->Snapshot();
  const auto links = link_system_->GetResolvedTopologyLinks();
  const auto link_system_id = link_system_->GetInstanceId();

  // Gather the flatland data into a vector of rectangle and image data that can be passed to
  // either the display controller directly or to the software renderer.
  std::vector<RenderData> image_list_per_display;
  for (const auto& [display_id, display_info] : display_map_) {
    const auto& transform = display_info.transform;
    const auto& resolution = display_info.pixel_scale;

    const auto topology_data =
        GlobalTopologyData::ComputeGlobalTopologyData(snapshot, links, link_system_id, transform);
    const auto global_matrices = ComputeGlobalMatrices(topology_data.topology_vector,
                                                       topology_data.parent_indices, snapshot);
    const auto [image_indices, images] =
        ComputeGlobalImageData(topology_data.topology_vector, snapshot);

    const auto image_rectangles =
        ComputeGlobalRectangles(SelectMatrices(global_matrices, image_indices));

    link_system_->UpdateLinks(topology_data.topology_vector, topology_data.child_counts,
                              topology_data.live_handles, global_matrices, resolution, snapshot);

    FX_DCHECK(image_rectangles.size() == images.size());
    image_list_per_display.push_back({.rectangles = std::move(image_rectangles),
                                      .images = std::move(images),
                                      .display_id = display_id});
  }
  return image_list_per_display;
}

void Engine::RenderFrame() {
  auto render_data_list = ComputeRenderData();

  // Create and set layers, one per image/rectangle, set the layer images and the
  // layer transforms. Afterwards we check the config, if it fails for whatever reason,
  // such as there being too many layers, then we fall back to software composition.
  for (auto& render_data : render_data_list) {
    // Every rectangle should have an associated image.
    uint32_t num_images = render_data.images.size();
    uint32_t num_rectangles = render_data.rectangles.size();
    auto& display_id = render_data.display_id;

    // TODO(fxbug.dev/59646): This should eventually be cached. We don't want to recreate the layers
    // every single time we call RenderFrame().
    std::vector<uint64_t> layers =
        CreateAndSetDisplayLayers(*display_controller_.get(), display_id, num_images);

    for (uint32_t i = 0; i < num_images; i++) {
      const auto& rectangle = render_data.rectangles[i];
      const auto& image = render_data.images[i];
      const auto& curr_layer_id = layers[i];

// TODO(fxbug.dev/59646): Add back in when we add testing for it.
#if 0
      // // Import each of the images.
      // // TODO(fxbug.dev/59646): This should eventually be cached.
      // uint64_t image_id = ImportImage(*display_controller_.get(), image);

      // // Set the imported image on the layer.
      // // TODO(fxbug.dev/59646): Add wait and signal events.
      // (*display_controller_.get())
      //     ->SetLayerImage(curr_layer_id, image_id, /*wait_event*/ 0, /*signal_event*/ 0);
#endif

      // Convert rectangle and image data into display controller source and destination frames.
      auto [src_frame, dst_frame] = RectangleDataToDisplayFrames(rectangle, image);

      // We just use the identity transform because the rectangles have already been rotated by
      // the flatland code.
      auto transform = fuchsia::hardware::display::Transform::IDENTITY;
      auto status = (*display_controller_.get())
                        ->SetLayerPrimaryPosition(curr_layer_id, transform, src_frame, dst_frame);
    }

// TODO(fxbug.dev/59646): Add back in when we have tests.
#if 0
  //   // Check that the display is capable of compositing all of the images we told it to.
  //   fuchsia::hardware::display::ConfigResult result;
  //   std::vector<fuchsia::hardware::display::ClientCompositionOp> ops;
  //   (*display_controller_.get())->CheckConfig(/*discard=*/false, &result, &ops);

  //   // If the results are ok, we can apply the config directly, else we have to composite ourselves.
  //   if (result == fuchsia::hardware::display::ConfigResult::OK) {
  //     auto status = (*display_controller_.get())->ApplyConfig();
  //     FX_DCHECK(status == ZX_OK);
  //   } else {
  //     // TODO(fxbug.dev/59646): Here is where we'd actually have to render using the 2D renderer.
  //   }
  // }
#endif
  }
}

// Register a new display to the engine.
void Engine::AddDisplay(uint64_t display_id, TransformHandle transform, glm::uvec2 pixel_scale) {
  display_map_[display_id] = {.transform = std::move(transform),
                              .pixel_scale = std::move(pixel_scale)};
}

}  // namespace flatland
