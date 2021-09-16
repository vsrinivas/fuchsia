// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/engine/display_compositor.h"

#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <zircon/pixelformat.h>

#include <cstdint>
#include <vector>

#include "src/ui/lib/escher/util/trace_macros.h"
#include "src/ui/scenic/lib/flatland/buffers/util.h"
#include "src/ui/scenic/lib/flatland/global_image_data.h"
#include "src/ui/scenic/lib/utils/helpers.h"

namespace flatland {

namespace {

// TODO(fxbug.dev/71344): We shouldn't need to provide the display controller with a pixel format.
const zx_pixel_format_t kDefaultImageFormat = ZX_PIXEL_FORMAT_ARGB_8888;

// Debugging color used to highlight images that have gone through the GPU rendering path.
const std::array<float, 4> kDebugColor = {0.9, 0.5, 0.5, 1};

// TODO(fxbug.dev/71410): Remove all references to zx_pixel_format_t.
fuchsia::sysmem::PixelFormatType ConvertZirconFormatToSysmemFormat(zx_pixel_format_t format) {
  switch (format) {
    // These two Zircon formats correspond to the Sysmem BGRA32 format.
    case ZX_PIXEL_FORMAT_RGB_x888:
    case ZX_PIXEL_FORMAT_ARGB_8888:
      return fuchsia::sysmem::PixelFormatType::BGRA32;
    case ZX_PIXEL_FORMAT_BGR_888x:
    case ZX_PIXEL_FORMAT_ABGR_8888:
      return fuchsia::sysmem::PixelFormatType::R8G8B8A8;
    case ZX_PIXEL_FORMAT_NV12:
      return fuchsia::sysmem::PixelFormatType::NV12;
  }
  FX_CHECK(false) << "Unsupported Zircon pixel format: " << format;
  return fuchsia::sysmem::PixelFormatType::INVALID;
}

// Returns a zircon format for buffer with this pixel format.
// TODO(fxbug.dev/71410): Remove all references to zx_pixel_format_t.
zx_pixel_format_t BufferCollectionPixelFormatToZirconFormat(
    fuchsia::sysmem::PixelFormat& pixel_format) {
  switch (pixel_format.type) {
    case fuchsia::sysmem::PixelFormatType::BGRA32:
      return ZX_PIXEL_FORMAT_ARGB_8888;
    case fuchsia::sysmem::PixelFormatType::R8G8B8A8:
      return ZX_PIXEL_FORMAT_ABGR_8888;
    case fuchsia::sysmem::PixelFormatType::NV12:
      return ZX_PIXEL_FORMAT_NV12;
    default:
      break;
  }
  FX_CHECK(false) << "Unsupported pixel format: " << static_cast<uint32_t>(pixel_format.type);
  return ZX_PIXEL_FORMAT_NONE;
}

// Returns an image type that describes the tiling format used for buffer with
// this pixel format. The values are display driver specific and not documented
// in display-controller.fidl.
// TODO(fxbug.dev/33334): Remove this when image type is removed from the display
// controller API.
uint32_t BufferCollectionPixelFormatToImageType(fuchsia::sysmem::PixelFormat& pixel_format) {
  if (pixel_format.has_format_modifier) {
    switch (pixel_format.format_modifier.value) {
      case fuchsia::sysmem::FORMAT_MODIFIER_INTEL_I915_X_TILED:
        return 1;  // IMAGE_TYPE_X_TILED
      case fuchsia::sysmem::FORMAT_MODIFIER_INTEL_I915_Y_TILED:
        return 2;  // IMAGE_TYPE_Y_LEGACY_TILED
      case fuchsia::sysmem::FORMAT_MODIFIER_INTEL_I915_YF_TILED:
        return 3;  // IMAGE_TYPE_YF_TILED
    }
  }
  return fuchsia::hardware::display::TYPE_SIMPLE;
}

}  // anonymous namespace

DisplayCompositor::DisplayCompositor(
    async_dispatcher_t* dispatcher,
    std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr> display_controller,
    const std::shared_ptr<Renderer>& renderer, fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator,
    BufferCollectionImportMode import_mode)
    : display_controller_(std::move(display_controller)),
      renderer_(renderer),
      release_fence_manager_(dispatcher),
      sysmem_allocator_(std::move(sysmem_allocator)),
      import_mode_(import_mode),
      weak_factory_(this) {
  FX_DCHECK(dispatcher);
  FX_DCHECK(renderer_);
  FX_DCHECK(sysmem_allocator_);
}

DisplayCompositor::~DisplayCompositor() {
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

bool DisplayCompositor::ImportBufferCollection(
    allocation::GlobalBufferCollectionId collection_id,
    fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
  FX_DCHECK(display_controller_);

  // Create a duped renderer token.
  auto sync_token = token.BindSync();
  fuchsia::sysmem::BufferCollectionTokenSyncPtr renderer_token;
  zx_status_t status = sync_token->Duplicate(ZX_RIGHT_SAME_RIGHTS, renderer_token.NewRequest());
  FX_DCHECK(status == ZX_OK);

  // Import the collection to the renderer.
  if (!renderer_->ImportBufferCollection(collection_id, sysmem_allocator,
                                         std::move(renderer_token))) {
    FX_LOGS(INFO) << "Renderer could not import buffer collection.";
    return false;
  }

  if (import_mode_ == BufferCollectionImportMode::RendererOnly) {
    status = sync_token->Close();
    FX_DCHECK(status == ZX_OK);
    return true;
  }

  // Create token for display. In EnforceDisplayConstraints mode, duplicate a token and pass it to
  // display. The allocation will fail if it the allocation is not directly displayable. In
  // AttemptDisplayConstraints mode, instead of passing a real token, we pass an AttachToken to
  // display. This way, display does not affect the allocation and we directly display if it happens
  // to work. In RendererOnly mode, we dont attempt directly displaying and fallback to renderer.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr display_token;
  if (import_mode_ == BufferCollectionImportMode::EnforceDisplayConstraints) {
    status = sync_token->Duplicate(ZX_RIGHT_SAME_RIGHTS, display_token.NewRequest());
    FX_DCHECK(status == ZX_OK);
    status = sync_token->Close();
    FX_DCHECK(status == ZX_OK);
  } else if (import_mode_ == BufferCollectionImportMode::AttemptDisplayConstraints) {
    fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection_sync_ptr;
    sysmem_allocator->BindSharedCollection(std::move(sync_token),
                                           buffer_collection_sync_ptr.NewRequest());
    status = buffer_collection_sync_ptr->Sync();
    FX_DCHECK(status == ZX_OK);
    // TODO(fxbug.dev/74423): Replace with prunable token when it is available.
    status =
        buffer_collection_sync_ptr->AttachToken(ZX_RIGHT_SAME_RIGHTS, display_token.NewRequest());
    FX_DCHECK(status == ZX_OK);
    status = buffer_collection_sync_ptr->Close();
    FX_DCHECK(status == ZX_OK);
  }

  // Duplicate display token to check later if attach token can be used in the allocated buffers.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr display_token_dup;
  status = display_token->Duplicate(ZX_RIGHT_SAME_RIGHTS, display_token_dup.NewRequest());
  FX_DCHECK(status == ZX_OK);
  status = display_token->Sync();
  FX_DCHECK(status == ZX_OK);
  fuchsia::sysmem::BufferCollectionSyncPtr display_token_sync_ptr;
  sysmem_allocator->BindSharedCollection(std::move(display_token),
                                         display_token_sync_ptr.NewRequest());
  {
    // Intentionally empty constraints. |display_token_sync_ptr| is used to detect logical
    // allocation completion and success or failure, as seen by the |renderer_token|, because
    // |display_token_sync_ptr| and |renderer_token| are in the same sysmem failure domain (child
    // domain of |buffer_collection_sync_ptr|).
    fuchsia::sysmem::BufferCollectionConstraints constraints;
    status = display_token_sync_ptr->SetConstraints(false, constraints);
    FX_DCHECK(status == ZX_OK);
  }
  display_tokens_[collection_id] = std::move(display_token_sync_ptr);

  // The image config needs to match the one passed to SetLayerPrimaryConfig for
  // the layer the image will be attached to.
  fuchsia::hardware::display::ImageConfig image_config;
  image_config.pixel_format = kDefaultImageFormat;
  image_config.type = 0;
  std::unique_lock<std::mutex> lock(lock_);
  auto result = scenic_impl::ImportBufferCollection(collection_id, *display_controller_.get(),
                                                    std::move(display_token_dup), image_config);

  return result;
}

void DisplayCompositor::ReleaseBufferCollection(
    allocation::GlobalBufferCollectionId collection_id) {
  std::unique_lock<std::mutex> lock(lock_);
  FX_DCHECK(display_controller_);
  (*display_controller_.get())->ReleaseBufferCollection(collection_id);
  renderer_->ReleaseBufferCollection(collection_id);
  display_tokens_.erase(collection_id);
  buffer_collection_supports_display_.erase(collection_id);
}

bool DisplayCompositor::ImportBufferImage(const allocation::ImageMetadata& metadata) {
  FX_DCHECK(display_controller_);

  if (metadata.identifier == 0) {
    FX_LOGS(ERROR) << "ImageMetadata identifier is invalid.";
    return false;
  }

  if (metadata.collection_id == allocation::kInvalidId) {
    FX_LOGS(ERROR) << "ImageMetadata collection ID is invalid.";
    return false;
  }

  if (metadata.width == 0 || metadata.height == 0) {
    FX_LOGS(ERROR) << "ImageMetadata has a null dimension: "
                   << "(" << metadata.width << ", " << metadata.height << ").";
    return false;
  }

  if (!renderer_->ImportBufferImage(metadata)) {
    FX_LOGS(ERROR) << "Renderer could not import image.";
    return false;
  }

  // ImportBufferImage() might be called to import client images or display images that we use as
  // render targets. For the second case, we still want to import image into the display. These
  // images have |buffer_collection_supports_display_| set as true in AddDisplay().
  if (import_mode_ == BufferCollectionImportMode::RendererOnly &&
      (buffer_collection_supports_display_.find(metadata.collection_id) ==
           buffer_collection_supports_display_.end() ||
       !buffer_collection_supports_display_[metadata.collection_id])) {
    buffer_collection_supports_display_[metadata.collection_id] = false;
    return true;
  }

  if (buffer_collection_supports_display_.find(metadata.collection_id) ==
      buffer_collection_supports_display_.end()) {
    zx_status_t allocation_status = ZX_OK;
    auto status =
        display_tokens_[metadata.collection_id]->CheckBuffersAllocated(&allocation_status);
    auto supports_display = status == ZX_OK && allocation_status == ZX_OK;
    buffer_collection_supports_display_[metadata.collection_id] = supports_display;
    if (supports_display) {
      fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info = {};
      status = display_tokens_[metadata.collection_id]->WaitForBuffersAllocated(
          &allocation_status, &buffer_collection_info);
      FX_DCHECK(status == ZX_OK) << "status: " << status;
      FX_DCHECK(allocation_status == ZX_OK) << "allocation_status: " << allocation_status;
      buffer_collection_pixel_format_[metadata.collection_id] =
          buffer_collection_info.settings.image_format_constraints.pixel_format;
    }
    status = display_tokens_[metadata.collection_id]->Close();
    display_tokens_.erase(metadata.collection_id);
  }

  if (!buffer_collection_supports_display_[metadata.collection_id]) {
    if (import_mode_ == BufferCollectionImportMode::AttemptDisplayConstraints) {
      // We fallback to renderer and continue if display isn't supported in
      // AttemptDisplayConstraints mode.
      return true;
    } else if (import_mode_ == BufferCollectionImportMode::EnforceDisplayConstraints) {
      return false;
    }
  }

  uint64_t display_image_id;
  FX_DCHECK(buffer_collection_pixel_format_.count(metadata.collection_id));
  auto pixel_format = buffer_collection_pixel_format_[metadata.collection_id];
  fuchsia::hardware::display::ImageConfig image_config = {
      .width = metadata.width,
      .height = metadata.height,
      .pixel_format = BufferCollectionPixelFormatToZirconFormat(pixel_format),
      .type = BufferCollectionPixelFormatToImageType(pixel_format)};
  zx_status_t import_image_status = ZX_OK;

  // Scope the lock.
  {
    std::unique_lock<std::mutex> lock(lock_);
    auto status = (*display_controller_.get())
                      ->ImportImage(image_config, metadata.collection_id, metadata.vmo_index,
                                    &import_image_status, &display_image_id);
    FX_DCHECK(status == ZX_OK);

    if (import_image_status != ZX_OK) {
      FX_LOGS(ERROR) << "Display controller could not import the image.";
      return false;
    }

    // Add the display-specific ID to the global map.
    image_id_map_[metadata.identifier] = display_image_id;
    return true;
  }
}

void DisplayCompositor::ReleaseBufferImage(allocation::GlobalImageId image_id) {
  auto display_image_id = InternalImageId(image_id);

  // Locks the rest of the function.
  std::unique_lock<std::mutex> lock(lock_);
  FX_DCHECK(display_controller_);
  (*display_controller_.get())->ReleaseImage(display_image_id);

  // Release image from the renderer.
  renderer_->ReleaseBufferImage(image_id);
}

uint64_t DisplayCompositor::CreateDisplayLayer() {
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

void DisplayCompositor::SetDisplayLayers(uint64_t display_id, const std::vector<uint64_t>& layers) {
  // Set all of the layers for each of the images on the display.
  std::unique_lock<std::mutex> lock(lock_);
  auto status = (*display_controller_.get())->SetDisplayLayers(display_id, layers);
  FX_DCHECK(status == ZX_OK);
}

bool DisplayCompositor::SetRenderDataOnDisplay(const RenderData& data) {
  // Every rectangle should have an associated image.
  uint32_t num_images = data.images.size();

  // Return early if we have an image that cannot be imported into display.
  for (uint32_t i = 0; i < num_images; i++) {
    if (InternalImageId(data.images[i].identifier) == allocation::kInvalidImageId)
      return false;
  }

  // Since we map 1 image to 1 layer, if there are more images than layers available for
  // the given display, then they cannot be directly composited to the display in hardware.
  std::vector<uint64_t> layers;
  {
    std::unique_lock<std::mutex> lock(lock_);
    auto it = display_engine_data_map_.find(data.display_id);
    FX_DCHECK(it != display_engine_data_map_.end());
    layers = it->second.layers;
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

void DisplayCompositor::ApplyLayerImage(uint32_t layer_id, escher::Rectangle2D rectangle,
                                        allocation::ImageMetadata image,
                                        scenic_impl::DisplayEventId wait_id,
                                        scenic_impl::DisplayEventId signal_id) {
  auto display_image_id = InternalImageId(image.identifier);
  auto [src, dst] = DisplaySrcDstFrames::New(rectangle, image);

  std::unique_lock<std::mutex> lock(lock_);

  // TODO(fxbug.dev/77993): The display controller pathway currently does not accurately take into
  // account rotation, even though the gpu rendering path does. While the gpu renderer can directly
  // make use of UV rotation to represent rotations, the display controller, making only use of a
  // source_rect (image sample region), will give false results with this current setup if a
  // rotation has been applied to the rectangle. On top of that, the current rectangle struct gives
  // no indication that it has been rotated, as the rotation is stored implicitly, meaning that we
  // cannot currently exit out of this pathway early if rotation is caught, nor can we accurately
  // choose the right transform. Therefore we will need explicit rotation data to be plumbed down to
  // be able to choose the right enum. This will be easier to do once we settle on the proper way to
  // handle transforms/matrices going forward.
  auto transform = fuchsia::hardware::display::Transform::IDENTITY;

  // TODO(fxbug.dev/71344): Pixel format should be ignored when using sysmem. We do not want to have
  // to deal with this default image format.
  FX_DCHECK(buffer_collection_pixel_format_.count(image.collection_id));
  auto pixel_format = buffer_collection_pixel_format_[image.collection_id];
  fuchsia::hardware::display::ImageConfig image_config = {
      .width = src.width,
      .height = src.height,
      .pixel_format = BufferCollectionPixelFormatToZirconFormat(pixel_format),
      .type = BufferCollectionPixelFormatToImageType(pixel_format)};

  (*display_controller_.get())->SetLayerPrimaryConfig(layer_id, image_config);

  (*display_controller_.get())->SetLayerPrimaryPosition(layer_id, transform, src, dst);

  auto alpha_mode = image.is_opaque ? fuchsia::hardware::display::AlphaMode::DISABLE
                                    : fuchsia::hardware::display::AlphaMode::PREMULTIPLIED;

  (*display_controller_.get())->SetLayerPrimaryAlpha(layer_id, alpha_mode, image.multiply_color[3]);

  // Set the imported image on the layer.
  (*display_controller_.get())->SetLayerImage(layer_id, display_image_id, wait_id, signal_id);
}

DisplayCompositor::DisplayConfigResponse DisplayCompositor::CheckConfig() {
  TRACE_DURATION("gfx", "flatland::DisplayCompositor::CheckConfig");
  fuchsia::hardware::display::ConfigResult result;
  std::vector<fuchsia::hardware::display::ClientCompositionOp> ops;
  std::unique_lock<std::mutex> lock(lock_);
  (*display_controller_.get())->CheckConfig(/*discard*/ false, &result, &ops);
  return {.result = result, .ops = ops};
}

void DisplayCompositor::DiscardConfig() {
  TRACE_DURATION("gfx", "flatland::DisplayCompositor::DiscardConfig");
  fuchsia::hardware::display::ConfigResult result;
  std::vector<fuchsia::hardware::display::ClientCompositionOp> ops;
  std::unique_lock<std::mutex> lock(lock_);
  (*display_controller_.get())->CheckConfig(/*discard*/ true, &result, &ops);
}

void DisplayCompositor::ApplyConfig() {
  TRACE_DURATION("gfx", "flatland::DisplayCompositor::ApplyConfig");
  std::unique_lock<std::mutex> lock(lock_);
  auto status = (*display_controller_.get())->ApplyConfig();
  FX_DCHECK(status == ZX_OK);
}

void DisplayCompositor::RenderFrame(uint64_t frame_number, zx::time presentation_time,
                                    const std::vector<RenderData>& render_data_list,
                                    std::vector<zx::event> release_fences,
                                    scheduling::FrameRenderer::FramePresentedCallback callback) {
  TRACE_DURATION("gfx", "flatland::DisplayCompositor::RenderFrame");
  TRACE_FLOW_STEP("gfx", "scenic_frame", frame_number);

  // Config should be reset before doing anything new.
  DiscardConfig();

  // Create and set layers, one per image/rectangle, set the layer images and the layer transforms.
  // Afterwards we check the config, if it fails for whatever reason, such as there being too many
  // layers, then we fall back to software composition.  Keep track of the display_ids, so that we
  // can pass them to the display controller.
  bool hardware_fail = false;
  std::vector<uint64_t> display_ids;
  for (auto& data : render_data_list) {
    display_ids.push_back(data.display_id);
    if (!SetRenderDataOnDisplay(data)) {
      // TODO(fxbug.dev/77416): just because setting the data on one display fails (e.g. due to too
      // many layers), that doesn't mean that all displays need to use GPU-composition.  Some day we
      // might want to use GPU-composition for some client images, and direct-scanout for others.
      hardware_fail = true;
      break;
    }
  }

  // Determine whether we need to fall back to GPU composition.  Avoid calling CheckConfig() if we
  // don't need to, because this requires a round-trip to the display controller.
  bool fallback_to_gpu_composition = false;
  if (hardware_fail) {
    fallback_to_gpu_composition = true;
  } else {
    auto [result, ops] = CheckConfig();
    fallback_to_gpu_composition = (result != fuchsia::hardware::display::ConfigResult::OK);
  }

  // If the results are not okay, we have to do GPU composition using the renderer.
  if (fallback_to_gpu_composition) {
    DiscardConfig();

    // Create an event that will be signaled when the final display's content has finished
    // rendering; it will be passed into |release_fence_manager_.OnGpuCompositedFrame()|.  If there
    // are multiple displays which require GPU-composited content, we pass this event to be signaled
    // when the final display's content has finished rendering (thus guaranteeing that all previous
    // content has also finished rendering).
    // TODO(fxbug.dev/77640): we might want to reuse events, instead of creating a new one every
    // frame.
    zx::event render_finished_fence = utils::CreateEvent();

    for (size_t i = 0; i < render_data_list.size(); ++i) {
      const bool is_final_display = (i + 1 == render_data_list.size());
      const auto& data = render_data_list[i];
      const auto it = display_engine_data_map_.find(data.display_id);
      FX_DCHECK(it != display_engine_data_map_.end());
      auto& display_engine_data = it->second;
      const uint32_t curr_vmo = display_engine_data.curr_vmo;
      display_engine_data.curr_vmo =
          (display_engine_data.curr_vmo + 1) % display_engine_data.vmo_count;
      FX_DCHECK(curr_vmo < display_engine_data.targets.size())
          << curr_vmo << "/" << display_engine_data.targets.size();
      FX_DCHECK(curr_vmo < display_engine_data.frame_event_datas.size())
          << curr_vmo << "/" << display_engine_data.frame_event_datas.size();

      const auto& render_target = display_engine_data.targets[curr_vmo];

      // Reset the event data.
      auto& event_data = display_engine_data.frame_event_datas[curr_vmo];

      // We expect the retired event to already have been signaled.  Verify this without waiting.
      {
        zx_status_t status =
            event_data.signal_event.wait_one(ZX_EVENT_SIGNALED, zx::time(), nullptr);
        if (status != ZX_OK) {
          FX_DCHECK(status == ZX_ERR_TIMED_OUT) << "unexpected status: " << status;
          FX_LOGS(ERROR)
              << "flatland::DisplayCompositor::RenderFrame rendering into in-use backbuffer";
        }
      }

      event_data.wait_event.signal(ZX_EVENT_SIGNALED, 0);
      event_data.signal_event.signal(ZX_EVENT_SIGNALED, 0);

      // Apply the debugging color to the images.
#ifdef VISUAL_DEBUGGING_ENABLED
      auto images = data.images;
      for (auto& image : images) {
        image.multiply_color[0] *= kDebugColor[0];
        image.multiply_color[1] *= kDebugColor[1];
        image.multiply_color[2] *= kDebugColor[2];
        image.multiply_color[3] *= kDebugColor[3];
      }
#else
      auto& images = data.images;
#endif  // VISUAL_DEBUGGING_ENABLED

      std::vector<zx::event> render_fences;
      render_fences.push_back(std::move(event_data.wait_event));
      // Only add render_finished_fence if we're rendering the final display's framebuffer.
      if (is_final_display) {
        render_fences.push_back(std::move(render_finished_fence));
        renderer_->Render(render_target, data.rectangles, images, render_fences);
        // Retrieve fence.
        render_finished_fence = std::move(render_fences.back());
      } else {
        renderer_->Render(render_target, data.rectangles, images, render_fences);
      }

      // Retrieve fence.
      event_data.wait_event = std::move(render_fences[0]);

      auto layer = display_engine_data.layers[0];
      SetDisplayLayers(data.display_id, {layer});
      ApplyLayerImage(layer, {glm::vec2(0), glm::vec2(render_target.width, render_target.height)},
                      render_target, event_data.wait_id, event_data.signal_id);

      auto [result, /*ops*/ _] = CheckConfig();
      if (result != fuchsia::hardware::display::ConfigResult::OK) {
        FX_LOGS(ERROR) << "Both display hardware composition and GPU rendering have failed.";

        // TODO(fxbug.dev/59646): Figure out how we really want to handle this case here.
        return;
      }
    }

    // See ReleaseFenceManager comments for details.
    FX_DCHECK(render_finished_fence);
    release_fence_manager_.OnGpuCompositedFrame(frame_number, std::move(render_finished_fence),
                                                std::move(release_fences), std::move(callback));
  } else {
    // See ReleaseFenceManager comments for details.
    release_fence_manager_.OnDirectScanoutFrame(frame_number, std::move(release_fences),
                                                std::move(callback));
  }

  // TODO(fxbug.dev/77414): we should be calling ApplyConfig2() here, but it's not implemented yet.
  // Additionally, if the previous frame was "direct scanout" (but not if "gpu composited") we
  // should obtain the fences for that frame and pass them directly to ApplyConfig2().
  // ReleaseFenceManager is somewhat poorly suited to this, because it was designed for an old
  // version of ApplyConfig2(), which latter proved to be infeasible for some drivers to implement.
  // For the time being, we fake a vsync event with a hardcoded timer.
  ApplyConfig();
  async::PostDelayedTask(
      async_get_default_dispatcher(),
      [weak = weak_factory_.GetWeakPtr(), frame_number, display_ids{std::move(display_ids)}]() {
        if (auto thiz = weak.get()) {
          for (auto display_id : display_ids) {
            thiz->OnVsync(display_id, frame_number, zx::time(zx_clock_get_monotonic()));
          }
        }
      },
      // Since Scenic's wakeup time is hardcoded to be 14ms from vsync, it is safe to assume that
      // waking up 14ms in the future will be after vsync has occurred.
      zx::duration(14'000'000));
}

void DisplayCompositor::OnVsync(uint64_t display_id, uint64_t frame_number, zx::time timestamp) {
  FX_DCHECK(display_id == 1) << "currently expect hardcoded display_id == 1, not " << display_id;
  TRACE_DURATION("gfx", "Flatland::DisplayCompositor::OnVsync");
  release_fence_manager_.OnVsync(frame_number, timestamp);
}

DisplayCompositor::FrameEventData DisplayCompositor::NewFrameEventData() {
  FrameEventData result;

  std::unique_lock<std::mutex> lock(lock_);

  // The DC waits on this to be signaled by the renderer.
  auto status = zx::event::create(0, &result.wait_event);
  FX_DCHECK(status == ZX_OK);
  result.wait_id = scenic_impl::ImportEvent(*display_controller_.get(), result.wait_event);
  FX_DCHECK(result.wait_id != fuchsia::hardware::display::INVALID_DISP_ID);

  // The DC signals this once it has set the layer image.  We pre-signal this event so the first
  // frame rendered with it behaves as though it was previously OKed for recycling.
  status = zx::event::create(0, &result.signal_event);
  FX_DCHECK(status == ZX_OK);
  result.signal_event.signal(0, ZX_EVENT_SIGNALED);
  result.signal_id = scenic_impl::ImportEvent(*display_controller_.get(), result.signal_event);
  FX_DCHECK(result.signal_id != fuchsia::hardware::display::INVALID_DISP_ID);

  return result;
}

allocation::GlobalBufferCollectionId DisplayCompositor::AddDisplay(
    uint64_t display_id, DisplayInfo info, uint32_t num_vmos,
    fuchsia::sysmem::BufferCollectionInfo_2* out_collection_info) {
  FX_DCHECK(display_engine_data_map_.find(display_id) == display_engine_data_map_.end())
      << "DisplayCompositor::AddDisplay(): display already exists: " << display_id;

  const uint32_t kWidth = info.dimensions.x;
  const uint32_t kHeight = info.dimensions.y;

  // Grab the best pixel format that the renderer prefers given the list of available formats on
  // the display.
  FX_DCHECK(info.formats.size());
  auto pixel_format = renderer_->ChoosePreferredPixelFormat(info.formats);

  display_info_map_[display_id] = std::move(info);
  auto& display_engine_data = display_engine_data_map_[display_id];

  // When we add in a new display, we create a couple of layers for that display upfront to be
  // used when we directly composite render data in hardware via the display controller.
  // TODO(fxbug.dev/77873): per-display layer lists are probably a bad idea; this approach doesn't
  // reflect the constraints of the underlying display hardware.
  for (uint32_t i = 0; i < 2; i++) {
    display_engine_data.layers.push_back(CreateDisplayLayer());
  }

  // Exit early if there are no vmos to create.
  if (num_vmos == 0) {
    return 0;
  }

  // If we are creating vmos, we need a non-null buffer collection pointer to return back
  // to the caller.
  FX_DCHECK(out_collection_info);

  // Create the buffer collection token to be used for frame buffers.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr compositor_token;
  auto status = sysmem_allocator_->AllocateSharedCollection(compositor_token.NewRequest());
  FX_DCHECK(status == ZX_OK) << status;

  // Dup the token for the renderer.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr renderer_token;
  status = compositor_token->Duplicate(std::numeric_limits<uint32_t>::max(),
                                       renderer_token.NewRequest());
  FX_DCHECK(status == ZX_OK);

  // Dup the token for the display.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr display_token;
  status =
      compositor_token->Duplicate(std::numeric_limits<uint32_t>::max(), display_token.NewRequest());
  FX_DCHECK(status == ZX_OK);

  // Register the buffer collection with the renderer
  auto collection_id = allocation::GenerateUniqueBufferCollectionId();
  auto result = renderer_->RegisterRenderTargetCollection(collection_id, sysmem_allocator_.get(),
                                                          std::move(renderer_token));
  FX_DCHECK(result);

  fuchsia::hardware::display::ImageConfig image_config;
  image_config.pixel_format = pixel_format;
  result = scenic_impl::ImportBufferCollection(collection_id, *display_controller_.get(),
                                               std::move(display_token), image_config);
  FX_DCHECK(result);

// Finally set the DisplayCompositor constraints.
#ifdef CPU_ACCESSIBLE_VMO
  auto [buffer_usage, memory_constraints] = GetUsageAndMemoryConstraintsForCpuWriteOften();
  fuchsia::sysmem::BufferCollectionSyncPtr collection_ptr =
      CreateBufferCollectionSyncPtrAndSetConstraints(
          sysmem_allocator_.get(), std::move(compositor_token), num_vmos, kWidth, kHeight,
          buffer_usage, ConvertZirconFormatToSysmemFormat(pixel_format), memory_constraints);
#else
  const fuchsia::sysmem::BufferUsage buffer_usage = {
      .vulkan = fuchsia::sysmem::vulkanUsageColorAttachment};
  fuchsia::sysmem::BufferCollectionSyncPtr collection_ptr =
      CreateBufferCollectionSyncPtrAndSetConstraints(
          sysmem_allocator_.get(), std::move(compositor_token), num_vmos, kWidth, kHeight,
          buffer_usage, ConvertZirconFormatToSysmemFormat(pixel_format));

#endif  // CPU_ACCESSIBLE_VMO

  // Have the client wait for buffers allocated so it can populate its information
  // struct with the vmo data.
  {
    zx_status_t allocation_status = ZX_OK;
    auto status = collection_ptr->WaitForBuffersAllocated(&allocation_status, out_collection_info);
    FX_DCHECK(status == ZX_OK) << "status: " << status;
    FX_DCHECK(allocation_status == ZX_OK) << "status: " << allocation_status;

    status = collection_ptr->Close();
    FX_DCHECK(status == ZX_OK);
  }

  // Import the images as well.
  for (uint32_t i = 0; i < num_vmos; i++) {
    allocation::ImageMetadata target = {.collection_id = collection_id,
                                        .identifier = allocation::GenerateUniqueImageId(),
                                        .vmo_index = i,
                                        .width = kWidth,
                                        .height = kHeight};
    display_engine_data.frame_event_datas.push_back(NewFrameEventData());
    display_engine_data.targets.push_back(target);
    // We know that this collection is supported by display because we collected constraints from
    // display in scenic_impl::ImportBufferCollection() and waited for successful allocation.
    buffer_collection_supports_display_[collection_id] = true;
    buffer_collection_pixel_format_[collection_id] =
        out_collection_info->settings.image_format_constraints.pixel_format;
    bool res = ImportBufferImage(target);
    FX_DCHECK(res);
  }

  display_engine_data.vmo_count = num_vmos;
  display_engine_data.curr_vmo = 0;
  return collection_id;
}

uint64_t DisplayCompositor::InternalImageId(allocation::GlobalImageId image_id) const {
  // Lock the whole function.
  std::unique_lock<std::mutex> lock(lock_);
  auto itr = image_id_map_.find(image_id);
  return itr == image_id_map_.end() ? allocation::kInvalidImageId : itr->second;
}

}  // namespace flatland
