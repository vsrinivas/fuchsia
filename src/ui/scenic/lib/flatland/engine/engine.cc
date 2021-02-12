// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/engine/engine.h"

#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <zircon/pixelformat.h>

#include <vector>

#include "src/ui/lib/escher/util/trace_macros.h"
#include "src/ui/scenic/lib/flatland/buffers/util.h"
#include "src/ui/scenic/lib/flatland/global_image_data.h"

namespace flatland {

const zx_pixel_format_t kDefaultImageFormat = ZX_PIXEL_FORMAT_ARGB_8888;

Engine::Engine(std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr> display_controller,
               const std::shared_ptr<Renderer>& renderer, RenderDataFunc render_data_func)
    : display_controller_(std::move(display_controller)),
      renderer_(renderer),
      render_data_func_(render_data_func) {
  FX_DCHECK(renderer_);
  FX_DCHECK(render_data_func_);
}

Engine::~Engine() {
  // Destroy all of the display layers.
  DiscardConfig();
  for (const auto& [_, data] : display_engine_data_map_) {
    for (const auto& layer : data.layers) {
      (*display_controller_.get())->DestroyLayer(layer);
    }
    for (const auto& event_data : data.frame_event_datas) {
      (*display_controller_.get())->ReleaseEvent(event_data.wait_id);
      (*display_controller_.get())->ReleaseEvent(event_data.signal_id);
    }
  }
}

bool Engine::ImportBufferCollection(
    sysmem_util::GlobalBufferCollectionId collection_id,
    fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
  FX_DCHECK(display_controller_);
  auto sync_token = token.BindSync();

  // TODO(fxbug.dev/61974): Find a way to query what formats are compatible with a particular
  // display.
  fuchsia::hardware::display::ImageConfig image_config = {.pixel_format = kDefaultImageFormat};

  std::unique_lock<std::mutex> lock(lock_);
  auto result = scenic_impl::ImportBufferCollection(collection_id, *display_controller_.get(),
                                                    std::move(sync_token), image_config);
  return result;
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

void Engine::ReleaseImage(sysmem_util::GlobalImageId image_id) {
  auto display_image_id = InternalImageId(image_id);

  // Locks the rest of the function.
  std::unique_lock<std::mutex> lock(lock_);
  FX_DCHECK(display_controller_);
  (*display_controller_.get())->ReleaseImage(display_image_id);
}

uint64_t Engine::CreateDisplayLayer() {
  std::unique_lock<std::mutex> lock(lock_);
  uint64_t layer_id;
  zx_status_t create_layer_status;
  zx_status_t transport_status =
      (*display_controller_.get())->CreateLayer(&create_layer_status, &layer_id);
  if (create_layer_status != ZX_OK || transport_status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create layer, " << create_layer_status;
    return 0;
  }
  return layer_id;
}

void Engine::SetDisplayLayers(uint64_t display_id, const std::vector<uint64_t>& layers) {
  // Set all of the layers for each of the images on the display.
  std::unique_lock<std::mutex> lock(lock_);
  auto status = (*display_controller_.get())->SetDisplayLayers(display_id, layers);
  FX_DCHECK(status == ZX_OK);
}

bool Engine::SetRenderDataOnDisplay(const RenderData& data) {
  // Every rectangle should have an associated image.
  uint32_t num_images = data.images.size();

  // Since we map 1 image to 1 layer, if there are more images than layers available for
  // the given display, then they cannot be directly composited to the display in hardware.
  std::vector<uint64_t> layers;
  {
    std::unique_lock<std::mutex> lock(lock_);
    layers = display_engine_data_map_[data.display_id].layers;
    if (layers.size() < num_images) {
      return false;
    }
  }

  // We only set as many layers as needed for the images we have.
  SetDisplayLayers(data.display_id,
                   std::vector<uint64_t>(layers.begin(), layers.begin() + num_images));

  for (uint32_t i = 0; i < num_images; i++) {
    ApplyLayerImage(layers[i], data.rectangles[i], data.images[i], /*wait_id*/ 0, /*signal_id*/ 0);
  }

  return true;
}

void Engine::ApplyLayerImage(uint32_t layer_id, escher::Rectangle2D rectangle, ImageMetadata image,
                             scenic_impl::DisplayEventId wait_id,
                             scenic_impl::DisplayEventId signal_id) {
  auto display_image_id = InternalImageId(image.identifier);
  auto [src, dst] = DisplaySrcDstFrames::New(rectangle, image);

  std::unique_lock<std::mutex> lock(lock_);

  // We just use the identity transform because the rectangles have already been rotated by
  // the flatland code.
  auto transform = fuchsia::hardware::display::Transform::IDENTITY;

  fuchsia::hardware::display::ImageConfig image_config = {
      .width = src.width, .height = src.height, .pixel_format = kDefaultImageFormat};

  (*display_controller_.get())->SetLayerPrimaryConfig(layer_id, image_config);

  (*display_controller_.get())->SetLayerPrimaryPosition(layer_id, transform, src, dst);

  auto alpha_mode = image.has_transparency ? fuchsia::hardware::display::AlphaMode::PREMULTIPLIED
                                           : fuchsia::hardware::display::AlphaMode::DISABLE;

  (*display_controller_.get())->SetLayerPrimaryAlpha(layer_id, alpha_mode, 1.f);

  // Set the imported image on the layer.
  // TODO(fxbug.dev/59646): Add wait and signal events.
  (*display_controller_.get())->SetLayerImage(layer_id, display_image_id, wait_id, signal_id);
}

Engine::DisplayConfigResponse Engine::CheckConfig() {
  TRACE_DURATION("flatland", "EngineRenderer::CheckConfig");
  fuchsia::hardware::display::ConfigResult result;
  std::vector<fuchsia::hardware::display::ClientCompositionOp> ops;
  std::unique_lock<std::mutex> lock(lock_);
  (*display_controller_.get())->CheckConfig(/*discard*/ false, &result, &ops);
  return {.result = result, .ops = ops};
}

void Engine::DiscardConfig() {
  TRACE_DURATION("flatland", "EngineRenderer::DiscardConfig");
  fuchsia::hardware::display::ConfigResult result;
  std::vector<fuchsia::hardware::display::ClientCompositionOp> ops;
  std::unique_lock<std::mutex> lock(lock_);
  (*display_controller_.get())->CheckConfig(/*discard*/ true, &result, &ops);
}

void Engine::ApplyConfig() {
  TRACE_DURATION("flatland", "EngineRenderer::ApplyConfig");
  std::unique_lock<std::mutex> lock(lock_);
  auto status = (*display_controller_.get())->ApplyConfig();
  FX_DCHECK(status == ZX_OK);
}

void Engine::RenderFrame() {
  TRACE_DURATION("flatland", "EngineRenderer::RenderFrame");
  auto render_data_list = render_data_func_(display_info_map_);

  // Config should be reset before doing anything new.
  DiscardConfig();

  // Create and set layers, one per image/rectangle, set the layer images and the
  // layer transforms. Afterwards we check the config, if it fails for whatever reason,
  // such as there being too many layers, then we fall back to software composition.
  bool hardware_fail = false;
  for (auto& data : render_data_list) {
    if (!SetRenderDataOnDisplay(data)) {
      hardware_fail = true;
      break;
    }
  }

  auto [result, ops] = CheckConfig();

  // If the results are not okay, we have to not do gpu composition using the renderer.
  if (hardware_fail || result != fuchsia::hardware::display::ConfigResult::OK) {
    DiscardConfig();

    for (const auto& data : render_data_list) {
      auto& display_engine_data = display_engine_data_map_[data.display_id];
      const auto& pixel_scale = display_info_map_[data.display_id].pixel_scale;
      auto& curr_vmo = display_engine_data.curr_vmo;
      const auto& render_target = display_engine_data.targets[curr_vmo];

      // Reset the event data.
      auto& event_data = display_engine_data.frame_event_datas[curr_vmo];

      // We expect the retired event to already have been signaled.  Verify this without waiting.
      if (event_data.signal_event.wait_one(ZX_EVENT_SIGNALED, zx::time(), nullptr) != ZX_OK) {
        FX_LOGS(ERROR) << "flatland::Engine::RenderFrame rendering into in-use backbuffer";
      }

      event_data.wait_event.signal(ZX_EVENT_SIGNALED, 0);
      event_data.signal_event.signal(ZX_EVENT_SIGNALED, 0);

      std::vector<zx::event> render_fences;
      render_fences.push_back(std::move(event_data.wait_event));
      renderer_->Render(render_target, data.rectangles, data.images, render_fences);
      curr_vmo = (curr_vmo + 1) % display_engine_data.vmo_count;
      event_data.wait_event = std::move(render_fences[0]);

      auto layer = display_engine_data.layers[0];
      SetDisplayLayers(data.display_id, {layer});
      ApplyLayerImage(layer, {glm::vec2(0), pixel_scale}, render_target, event_data.wait_id,
                      event_data.signal_id);

      auto [result, /*ops*/ _] = CheckConfig();
      if (result != fuchsia::hardware::display::ConfigResult::OK) {
        FX_LOGS(ERROR) << "Both display hardware composition and GPU rendering have failed.";

        // TODO(fxbug.dev/59646): Figure out how we really want to handle this case here.
        return;
      }
    }
  }

  ApplyConfig();
}

Engine::FrameEventData Engine::NewFrameEventData() {
  FrameEventData result;

  std::unique_lock<std::mutex> lock(lock_);

  // The DC waits on this to be signaled by the renderer.
  auto status = zx::event::create(0, &result.wait_event);
  FX_DCHECK(status == ZX_OK);
  result.wait_id = scenic_impl::ImportEvent(*display_controller_.get(), result.wait_event);
  FX_DCHECK(result.wait_id != fuchsia::hardware::display::INVALID_DISP_ID);

  // The DC signals this once it has set the layer image.
  status = zx::event::create(0, &result.signal_event);
  FX_DCHECK(status == ZX_OK);
  result.signal_id = scenic_impl::ImportEvent(*display_controller_.get(), result.signal_event);
  FX_DCHECK(result.signal_id != fuchsia::hardware::display::INVALID_DISP_ID);

  return result;
}

sysmem_util::GlobalBufferCollectionId Engine::AddDisplay(
    uint64_t display_id, DisplayInfo info, fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
    uint32_t num_vmos, fuchsia::sysmem::BufferCollectionInfo_2* collection_info) {
  FX_DCHECK(sysmem_allocator);
  const uint32_t kWidth = info.pixel_scale.x;
  const uint32_t kHeight = info.pixel_scale.y;

  display_info_map_[display_id] = std::move(info);
  auto& display_engine_data = display_engine_data_map_[display_id];

  // When we add in a new display, we create a couple of layers for that display upfront to be
  // used when we directly composite render data in hardware via the display controller.
  // TODO(fx.dev/66499): Right now we're just hardcoding the number of layers per display
  // uniformly, but this should probably be handled more dynamically in the future when we're
  // dealing with displays that can potentially handle many more layers. Although Astro can only
  // handle 1 layer right now, we create 2 layers in order to do more complicated unit testing
  // with the mock display controller.
  for (uint32_t i = 0; i < 2; i++) {
    display_engine_data.layers.push_back(CreateDisplayLayer());
  }

  // Exit early if there are no vmos to create.
  if (num_vmos == 0) {
    return 0;
  }

  FX_DCHECK(collection_info);

  // Create the buffer collection token to be used for frame buffers.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr engine_token;
  auto status = sysmem_allocator->AllocateSharedCollection(engine_token.NewRequest());
  FX_DCHECK(status == ZX_OK) << status;

  // Dup the token for the renderer.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr renderer_token;
  status =
      engine_token->Duplicate(std::numeric_limits<uint32_t>::max(), renderer_token.NewRequest());
  FX_DCHECK(status == ZX_OK);

  // Dup the token for the display.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr display_token;
  status =
      engine_token->Duplicate(std::numeric_limits<uint32_t>::max(), display_token.NewRequest());
  FX_DCHECK(status == ZX_OK);

  // Register the buffer collection with the renderer
  auto renderer_id = sysmem_util::GenerateUniqueBufferCollectionId();
  auto result = renderer_->RegisterRenderTargetCollection(renderer_id, sysmem_allocator,
                                                          std::move(renderer_token));
  FX_DCHECK(result);

  // Register the buffer collection with the display controller.
  result = ImportBufferCollection(renderer_id, sysmem_allocator, std::move(display_token));
  FX_DCHECK(result);

  // Finally set the engine constraints.
  auto [buffer_usage, memory_constraints] = GetUsageAndMemoryConstraintsForCpuWriteOften();
  fuchsia::sysmem::BufferCollectionSyncPtr collection_ptr =
      CreateClientPointerWithConstraints(sysmem_allocator, std::move(engine_token), num_vmos,
                                         kWidth, kHeight, buffer_usage, memory_constraints);

  // Have the client wait for buffers allocated so it can populate its information
  // struct with the vmo data.
  {
    zx_status_t allocation_status = ZX_OK;
    auto status = collection_ptr->WaitForBuffersAllocated(&allocation_status, collection_info);
    FX_DCHECK(status == ZX_OK);
    FX_DCHECK(allocation_status == ZX_OK);

    status = collection_ptr->Close();
    FX_DCHECK(status == ZX_OK);
  }

  // Import the images as well.
  for (uint32_t i = 0; i < num_vmos; i++) {
    ImageMetadata target = {.collection_id = renderer_id,
                            .identifier = sysmem_util::GenerateUniqueImageId(),
                            .vmo_idx = i,
                            .width = kWidth,
                            .height = kHeight,
                            .has_transparency = false};

    display_engine_data.frame_event_datas.push_back(NewFrameEventData());
    display_engine_data.targets.push_back(target);
    bool res = ImportImage(target);
    FX_DCHECK(res);

    res = renderer_->ImportImage(target);
    FX_DCHECK(res);
  }

  display_engine_data.vmo_count = num_vmos;
  display_engine_data.curr_vmo = 0;
  return renderer_id;
}

uint64_t Engine::InternalImageId(sysmem_util::GlobalImageId image_id) const {
  // Lock the whole function.
  std::unique_lock<std::mutex> lock(lock_);
  auto itr = image_id_map_.find(image_id);
  FX_DCHECK(itr != image_id_map_.end());
  return itr->second;
}

}  // namespace flatland
