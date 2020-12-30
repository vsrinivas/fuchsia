// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/engine/engine.h"

#include <lib/fdio/directory.h>
#include <zircon/pixelformat.h>

#include <vector>

#include "src/ui/scenic/lib/display/util.h"
#include "src/ui/scenic/lib/flatland/buffers/util.h"
#include "src/ui/scenic/lib/flatland/global_image_data.h"

namespace flatland {

namespace {

const zx_pixel_format_t kDefaultImageFormat = ZX_PIXEL_FORMAT_ARGB_8888;

// Struct to combine the source and destination frames used to set a layer's
// position on the display. The src frame represents the (cropped) UV coordinates
// of the image and the dst frame represents the position in screen space that
// the layer will be placed.
struct DisplayFrameData {
  fuchsia::hardware::display::Frame src;
  fuchsia::hardware::display::Frame dst;
};

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

void SetDisplayLayers(fuchsia::hardware::display::ControllerSyncPtr& display_controller,
                      uint64_t display_id, const std::vector<uint64_t>& layers) {
  // Set all of the layers for each of the images on the display.
  auto status = display_controller->SetDisplayLayers(display_id, layers);
  FX_DCHECK(status == ZX_OK);
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

Engine::Engine(std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr> display_controller,
               const std::shared_ptr<Renderer>& renderer,
               const std::shared_ptr<LinkSystem>& link_system,
               const std::shared_ptr<UberStructSystem>& uber_struct_system)
    : display_controller_(std::move(display_controller)),
      renderer_(renderer),
      link_system_(link_system),
      uber_struct_system_(uber_struct_system) {
  FX_DCHECK(renderer_);
  FX_DCHECK(link_system_);
  FX_DCHECK(uber_struct_system_);
}

Engine::~Engine() {
  // Destroy all of the display layers.
  for (const auto& [_, layers] : display_layer_map_) {
    for (const auto& layer : layers) {
      (*display_controller_.get())->DestroyLayer(layer);
    }
  }

  uber_struct_system_.reset();
  link_system_.reset();
}

bool Engine::ImportBufferCollection(
    sysmem_util::GlobalBufferCollectionId collection_id,
    fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
  FX_DCHECK(display_controller_);
  auto sync_token = token.BindSync();

  // TODO(fxbug.dev/61974): Find a way to query what formats are compatible with a particular
  // display. Right now we hardcode ARGB_8888 to match the format used in tests for textures.
  fuchsia::hardware::display::ImageConfig image_config = {.pixel_format = kDefaultImageFormat};

  // Scope the lock.
  {
    std::unique_lock<std::mutex> lock(lock_);
    auto result = scenic_impl::ImportBufferCollection(collection_id, *display_controller_.get(),
                                                      std::move(sync_token), image_config);
    return result;
  }
}

void Engine::ReleaseBufferCollection(sysmem_util::GlobalBufferCollectionId collection_id) {
  std::unique_lock<std::mutex> lock(lock_);
  FX_DCHECK(display_controller_);
  (*display_controller_.get())->ReleaseBufferCollection(collection_id);
}

bool Engine::ImportImage(const ImageMetadata& meta_data) {
  FX_DCHECK(display_controller_);

  if (meta_data.identifier == 0) {
    FX_LOGS(ERROR) << "ImageMetadata identifier is invalid.";
    return false;
  }

  if (meta_data.collection_id == sysmem_util::kInvalidId) {
    FX_LOGS(ERROR) << "ImageMetadata collection ID is invalid.";
    return false;
  }

  if (meta_data.width == 0 || meta_data.height == 0) {
    FX_LOGS(ERROR) << "ImageMetadata has a null dimension: "
                   << "(" << meta_data.width << ", " << meta_data.height << ").";
    return false;
  }

  uint64_t display_image_id;
  fuchsia::hardware::display::ImageConfig image_config = {
      .width = meta_data.width,
      .height = meta_data.height,
      // TODO(fxbug.dev/61974): Find a way to query what formats are compatible with a particular
      // display. Right now we hardcode ARGB_8888 to match the format used in tests for textures.
      .pixel_format = kDefaultImageFormat};
  zx_status_t import_image_status = ZX_OK;

  // Scope the lock.
  {
    std::unique_lock<std::mutex> lock(lock_);
    auto status = (*display_controller_.get())
                      ->ImportImage(image_config, meta_data.collection_id, meta_data.vmo_idx,
                                    &import_image_status, &display_image_id);
    FX_DCHECK(status == ZX_OK);

    if (import_image_status != ZX_OK) {
      FX_LOGS(ERROR) << "Display controller could not import the image.";
      return false;
    }

    // Add the display-specific ID to the global map.
    image_id_map_[meta_data.identifier] = display_image_id;
    return true;
  }
}

void Engine::ReleaseImage(GlobalImageId image_id) {
  auto display_image_id = InternalImageId(image_id);

  // Locks the rest of the function.
  std::unique_lock<std::mutex> lock(lock_);
  FX_DCHECK(display_controller_);
  (*display_controller_.get())->ReleaseImage(display_image_id);
}

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

    link_system_->UpdateLinks(topology_data.topology_vector, topology_data.live_handles,
                              global_matrices, resolution, snapshot);

    FX_DCHECK(image_rectangles.size() == images.size());
    image_list_per_display.push_back({.rectangles = std::move(image_rectangles),
                                      .images = std::move(images),
                                      .display_id = display_id});
  }
  return image_list_per_display;
}

bool Engine::SetLayers(const RenderData& data) {
  // Every rectangle should have an associated image.
  uint32_t num_images = data.images.size();

  // Since we map 1 image to 1 layer, if there are more images than layers available for
  // the given display, then they cannot be directly composited to the display in hardware.
  std::vector<uint64_t> layers;
  {
    std::unique_lock<std::mutex> lock(lock_);
    layers = display_layer_map_[data.display_id];
    if (layers.size() < num_images) {
      return false;
    }

    // We only set as many layers as needed for the images we have.
    SetDisplayLayers(*display_controller_.get(), data.display_id,
                     std::vector<uint64_t>(layers.begin(), layers.begin() + num_images));
  }

  for (uint32_t i = 0; i < num_images; i++) {
    ApplyLayerImage(layers[i], data.rectangles[i], data.images[i]);
  }

  return true;
}

void Engine::ApplyLayerImage(uint32_t layer_id, escher::Rectangle2D rectangle,
                             ImageMetadata image) {
  auto display_image_id = InternalImageId(image.identifier);
  auto [src, dst] = RectangleDataToDisplayFrames(rectangle, image);

  std::unique_lock<std::mutex> lock(lock_);

  // We just use the identity transform because the rectangles have already been rotated by
  // the flatland code.
  auto transform = fuchsia::hardware::display::Transform::IDENTITY;

  fuchsia::hardware::display::ImageConfig image_config = {
      .width = src.width, .height = src.height, .pixel_format = kDefaultImageFormat};

  (*display_controller_.get())->SetLayerPrimaryConfig(layer_id, image_config);

  (*display_controller_.get())->SetLayerPrimaryPosition(layer_id, transform, src, dst);

  // TODO:(fxbug.dev/52632): Once we expose transparency in the flatland API, we can undisable
  // the alpha mode for the display controller.
  (*display_controller_.get())
      ->SetLayerPrimaryAlpha(layer_id, fuchsia::hardware::display::AlphaMode::DISABLE, 1.f);

  // Set the imported image on the layer.
  // TODO(fxbug.dev/59646): Add wait and signal events.
  (*display_controller_.get())
      ->SetLayerImage(layer_id, display_image_id,
                      /*wait_event*/ 0, /*signal_event*/ 0);
}

Engine::DisplayConfigResponse Engine::CheckConfig(bool discard) {
  fuchsia::hardware::display::ConfigResult result;
  std::vector<fuchsia::hardware::display::ClientCompositionOp> ops;
  std::unique_lock<std::mutex> lock(lock_);
  (*display_controller_.get())->CheckConfig(discard, &result, &ops);
  return {.result = result, .ops = ops};
}

void Engine::ApplyConfig() {
  std::unique_lock<std::mutex> lock(lock_);
  auto status = (*display_controller_.get())->ApplyConfig();
  FX_DCHECK(status == ZX_OK);
}

void Engine::RenderFrame() {
  auto render_data_list = ComputeRenderData();

  // Create and set layers, one per image/rectangle, set the layer images and the
  // layer transforms. Afterwards we check the config, if it fails for whatever reason,
  // such as there being too many layers, then we fall back to software composition.
  bool hardware_fail = false;
  for (auto& data : render_data_list) {
    if (!SetLayers(data)) {
      hardware_fail = true;
      break;
    }
  }

  auto [result, ops] = CheckConfig(/*discard*/ false);

  // If the results are not okay, we have to not do gpu composition using the renderer.
  if (hardware_fail || result != fuchsia::hardware::display::ConfigResult::OK) {
    // TODO(fxbug.dev/59646): Here is where we'd actually have to render using the 2D renderer.
    // This involves discarding the above config, redoing the config for the software rendering
    // path and then calling CheckConfig() once more.
    for (auto op : ops) {
      FX_LOGS(INFO) << "Op display id: " << op.display_id;
      FX_LOGS(INFO) << "Op layer id: " << op.layer_id;
      FX_LOGS(INFO) << "Op Code: " << static_cast<uint32_t>(op.opcode);
    }

    FX_LOGS(ERROR) << "Engine hit unimplemented software rendering path. This should not happen "
                      "in current test environments.";
    FX_DCHECK(false);
  }

  ApplyConfig();
}

void Engine::AddDisplay(uint64_t display_id, TransformHandle transform, glm::uvec2 pixel_scale) {
  display_map_[display_id] = {.transform = std::move(transform),
                              .pixel_scale = std::move(pixel_scale)};

  // When we add in a new display, we create a couple of layers for that display upfront to be used
  // when we directly composite render data in hardware via the display controller.
  // TODO(fx.dev/66499): Right now we're just hardcoding the number of layers per display uniformly,
  // but this should probably be handled more dynamically in the future when we're dealing with
  // displays that can potentially handle many more layers. Although Astro can only handle 1 layer
  // right now, we create 2 layers in order to do more complicated unit testing with the mock
  // display controller.
  for (uint32_t i = 0; i < 2; i++) {
    display_layer_map_[display_id].push_back(InitializeDisplayLayer(*display_controller_.get()));
  }
}

sysmem_util::GlobalBufferCollectionId Engine::RegisterTargetCollection(
    fuchsia::sysmem::Allocator_Sync* sysmem_allocator, uint64_t display_id, uint32_t num_vmos) {
  FX_DCHECK(sysmem_allocator);
  auto iter = display_map_.find(display_id);
  if (iter == display_map_.end() || num_vmos == 0) {
    return sysmem_util::kInvalidId;
  }

  auto display_info = iter->second;

  const uint32_t width = display_info.pixel_scale.x;
  const uint32_t height = display_info.pixel_scale.y;

  // Create the buffer collection token to be used for frame buffers.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr engine_token;
  auto status = sysmem_allocator->AllocateSharedCollection(engine_token.NewRequest());
  FX_DCHECK(status == ZX_OK) << status;

  // Dup the token for the renderer.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr renderer_token;
  {
    status =
        engine_token->Duplicate(std::numeric_limits<uint32_t>::max(), renderer_token.NewRequest());
    FX_DCHECK(status == ZX_OK);
  }

  // Dup the token for the display.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr display_token;
  {
    status =
        engine_token->Duplicate(std::numeric_limits<uint32_t>::max(), display_token.NewRequest());
    FX_DCHECK(status == ZX_OK);
  }

  // Register the buffer collection with the renderer
  auto renderer_collection_id = sysmem_util::GenerateUniqueBufferCollectionId();
  auto result = renderer_->RegisterRenderTargetCollection(renderer_collection_id, sysmem_allocator,
                                                          std::move(renderer_token));
  FX_DCHECK(result);

  // Register the buffer collection with the display controller.
  result =
      ImportBufferCollection(renderer_collection_id, sysmem_allocator, std::move(display_token));
  FX_DCHECK(result);

  // Finally set the engine constraints.
  SetClientConstraintsAndWaitForAllocated(sysmem_allocator, std::move(engine_token), num_vmos,
                                          width, height, kNoneUsage, std::nullopt);

  return renderer_collection_id;
}

uint64_t Engine::InternalImageId(GlobalImageId image_id) const {
  // Lock the whole function.
  std::unique_lock<std::mutex> lock(lock_);
  auto itr = image_id_map_.find(image_id);
  FX_DCHECK(itr != image_id_map_.end());
  return itr->second;
}

}  // namespace flatland
