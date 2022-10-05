// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/engine/display_compositor.h"

#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <lib/trace/event.h>
#include <zircon/pixelformat.h>

#include <cstdint>
#include <vector>

#include "src/ui/lib/escher/util/trace_macros.h"
#include "src/ui/scenic/lib/flatland/buffers/util.h"
#include "src/ui/scenic/lib/flatland/global_image_data.h"
#include "src/ui/scenic/lib/utils/helpers.h"

namespace flatland {

namespace {

// Debugging color used to highlight images that have gone through the GPU rendering path.
const std::array<float, 4> kDebugColor = {0.9, 0.5, 0.5, 1};

#ifdef CPU_ACCESSIBLE_VMO
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
#endif  // CPU_ACCESSIBLE_VMO

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
    case fuchsia::sysmem::PixelFormatType::I420:
      return ZX_PIXEL_FORMAT_I420;
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

fuchsia::hardware::display::AlphaMode GetAlphaMode(
    const fuchsia::ui::composition::BlendMode& blend_mode) {
  fuchsia::hardware::display::AlphaMode alpha_mode;
  switch (blend_mode) {
    case fuchsia::ui::composition::BlendMode::SRC:
      alpha_mode = fuchsia::hardware::display::AlphaMode::DISABLE;
      break;
    case fuchsia::ui::composition::BlendMode::SRC_OVER:
      alpha_mode = fuchsia::hardware::display::AlphaMode::PREMULTIPLIED;
      break;
  }
  return alpha_mode;
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
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
    BufferCollectionUsage usage, std::optional<fuchsia::math::SizeU> size) {
  TRACE_DURATION("gfx", "flatland::DisplayCompositor::ImportBufferCollection");
  FX_DCHECK(display_controller_);
  // Expect the default Buffer Collection usage type.
  FX_DCHECK(usage == BufferCollectionUsage::kClientImage);

  // Create a duped renderer token.
  auto sync_token = token.BindSync();
  fuchsia::sysmem::BufferCollectionTokenSyncPtr renderer_token;
  zx_status_t status = sync_token->Duplicate(ZX_RIGHT_SAME_RIGHTS, renderer_token.NewRequest());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot duplicate token. The client may have invalidated the token.";
    return false;
  }

  // Import the collection to the renderer.
  if (!renderer_->ImportBufferCollection(collection_id, sysmem_allocator, std::move(renderer_token),
                                         usage, size)) {
    FX_LOGS(INFO) << "Renderer could not import buffer collection.";
    return false;
  }

  if (import_mode_ == BufferCollectionImportMode::RendererOnly) {
    status = sync_token->Close();
    return true;
  }

  // Create token for display. In EnforceDisplayConstraints mode, duplicate a token and pass it to
  // display. The allocation will fail if it the allocation is not directly displayable. In
  // AttemptDisplayConstraints mode, instead of passing a real token, we pass an AttachToken to
  // display. This way, display does not affect the allocation and we directly display if it happens
  // to work. In RendererOnly mode, we don't attempt directly displaying and fallback to renderer.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr display_token;
  if (import_mode_ == BufferCollectionImportMode::EnforceDisplayConstraints) {
    status = sync_token->Duplicate(ZX_RIGHT_SAME_RIGHTS, display_token.NewRequest());
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Cannot duplicate token. The client may have invalidated the token.";
      return false;
    }
    status = sync_token->Close();
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Cannot close token. The client may have invalidated the token.";
      return false;
    }
  } else if (import_mode_ == BufferCollectionImportMode::AttemptDisplayConstraints) {
    fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection_sync_ptr;
    sysmem_allocator->BindSharedCollection(std::move(sync_token),
                                           buffer_collection_sync_ptr.NewRequest());
    status = buffer_collection_sync_ptr->Sync();
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Cannot sync token. The client may have invalidated the token.";
      return false;
    }
    // TODO(fxbug.dev/74423): Replace with prunable token when it is available.
    status =
        buffer_collection_sync_ptr->AttachToken(ZX_RIGHT_SAME_RIGHTS, display_token.NewRequest());
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Cannot create AttachToken. The client may have invalidated the token.";
      return false;
    }
    status = buffer_collection_sync_ptr->Close();
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Cannot close token. The client may have invalidated the token.";
      return false;
    }
  } else {
    // BufferCollectionImportMode::RendererOnly was handled above.
    FX_NOTREACHED();
  }

  // Duplicate display token to check later if attach token can be used in the allocated buffers.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr display_token_dup;
  status = display_token->Duplicate(ZX_RIGHT_SAME_RIGHTS, display_token_dup.NewRequest());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot duplicate token. The client may have invalidated the token.";
    return false;
  }
  status = display_token->Sync();
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot sync token. The client may have invalidated the token.";
    return false;
  }
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
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Cannot set constraints. The client may have invalidated the token.";
      return false;
    }
  }
  display_tokens_[collection_id] = std::move(display_token_sync_ptr);

  // Set image config fields to zero to indicate that a specific size, format, or type is
  // not required.
  fuchsia::hardware::display::ImageConfig image_config;
  image_config.pixel_format = ZX_PIXEL_FORMAT_NONE;
  image_config.type = 0;
  std::unique_lock<std::mutex> lock(lock_);
  auto result = scenic_impl::ImportBufferCollection(collection_id, *display_controller_.get(),
                                                    std::move(display_token_dup), image_config);

  return result;
}

void DisplayCompositor::ReleaseBufferCollection(allocation::GlobalBufferCollectionId collection_id,
                                                BufferCollectionUsage usage) {
  TRACE_DURATION("gfx", "flatland::DisplayCompositor::ReleaseBufferCollection");
  FX_DCHECK(usage == BufferCollectionUsage::kClientImage);
  std::unique_lock<std::mutex> lock(lock_);
  FX_DCHECK(display_controller_);
  (*display_controller_.get())->ReleaseBufferCollection(collection_id);
  renderer_->ReleaseBufferCollection(collection_id, usage);
  display_tokens_.erase(collection_id);
  buffer_collection_supports_display_.erase(collection_id);
}

bool DisplayCompositor::ImportBufferImage(const allocation::ImageMetadata& metadata,
                                          BufferCollectionUsage usage) {
  TRACE_DURATION("gfx", "flatland::DisplayCompositor::ImportBufferImage");

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

  if (!renderer_->ImportBufferImage(metadata, usage)) {
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
      if (status != ZX_OK || allocation_status != ZX_OK) {
        FX_LOGS(ERROR) << "WaitForBuffersAllocated failed: " << status << ":" << allocation_status;
        return false;
      }
      buffer_collection_pixel_format_[metadata.collection_id] =
          buffer_collection_info.settings.image_format_constraints.pixel_format;
    }
    status = display_tokens_[metadata.collection_id]->Close();
    display_tokens_.erase(metadata.collection_id);
  }

  // TODO(fxbug.dev/85601): Remove after YUV buffers can be imported to display. We filter YUV
  // images out of display path.
  if (buffer_collection_pixel_format_[metadata.collection_id].type ==
          fuchsia::sysmem::PixelFormatType::NV12 ||
      buffer_collection_pixel_format_[metadata.collection_id].type ==
          fuchsia::sysmem::PixelFormatType::I420) {
    buffer_collection_supports_display_[metadata.collection_id] = false;
    return true;
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
                      ->ImportImage2(image_config, metadata.collection_id, metadata.identifier,
                                     metadata.vmo_index, &import_image_status);
    FX_DCHECK(status == ZX_OK);

    if (import_image_status != ZX_OK) {
      FX_LOGS(ERROR) << "Display controller could not import the image.";
      return false;
    }

    // Add the display-specific ID to the global map.
    return true;
  }
}

void DisplayCompositor::ReleaseBufferImage(allocation::GlobalImageId image_id) {
  TRACE_DURATION("gfx", "flatland::DisplayCompositor::ReleaseBufferImage");

  // Locks the rest of the function.
  std::unique_lock<std::mutex> lock(lock_);
  FX_DCHECK(display_controller_);
  (*display_controller_.get())->ReleaseImage(image_id);

  // Release image from the renderer.
  renderer_->ReleaseBufferImage(image_id);

  image_event_map_.erase(image_id);
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

  for (uint32_t i = 0; i < num_images; i++) {
    const uint32_t image_id = data.images[i].identifier;
    if (image_event_map_.find(image_id) == image_event_map_.end()) {
      image_event_map_[image_id] = NewImageEventData();
    } else {
      // If the event is not signaled, image must still be in use by the display and cannot be used
      // again.
      auto status =
          image_event_map_[image_id].signal_event.wait_one(ZX_EVENT_SIGNALED, zx::time(), nullptr);
      if (status != ZX_OK) {
        return false;
      }
    }
    pending_images_in_config_.push_back(image_id);
  }

  // We only set as many layers as needed for the images we have.
  SetDisplayLayers(data.display_id,
                   std::vector<uint64_t>(layers.begin(), layers.begin() + num_images));

  for (uint32_t i = 0; i < num_images; i++) {
    const uint32_t image_id = data.images[i].identifier;
    if (image_id != allocation::kInvalidImageId) {
      if (buffer_collection_supports_display_[data.images[i].collection_id]) {
        ApplyLayerImage(layers[i], data.rectangles[i], data.images[i], /*wait_id*/ 0,
                        /*signal_id*/ image_event_map_[image_id].signal_id);
      } else {
        return false;
      }
    } else {
      // TODO(fxbug.dev/104887): Not all display hardware is able to handle color layers with
      // specific sizes, which is required for doing solid-fill rects on the display path.
      // If we encounter one of those rects here -- unless it is the backmost layer and fullscreen
      // -- then we abort.
      const auto& rect = data.rectangles[i];
      const auto& display_size = display_info_map_[data.display_id].dimensions;
      if (i == 0 && rect.origin.x == 0 && rect.origin.y == 0 && rect.extent.x == display_size.x &&
          rect.extent.y == display_size.y) {
        ApplyLayerColor(layers[i], rect, data.images[i]);
      } else {
        return false;
      }
    }
  }
  return true;
}

void DisplayCompositor::ApplyLayerColor(uint32_t layer_id, escher::Rectangle2D rectangle,
                                        allocation::ImageMetadata image) {
  std::unique_lock<std::mutex> lock(lock_);

  // We have to convert the image_metadata's multiply color, which is an array of normalized
  // floating point values, to an unnormalized array of uint8_ts in the range 0-255.
  std::vector<uint8_t> col = {static_cast<uint8_t>(255 * image.multiply_color[0]),
                              static_cast<uint8_t>(255 * image.multiply_color[1]),
                              static_cast<uint8_t>(255 * image.multiply_color[2]),
                              static_cast<uint8_t>(255 * image.multiply_color[3])};

  (*display_controller_.get())->SetLayerColorConfig(layer_id, ZX_PIXEL_FORMAT_ARGB_8888, col);

// TODO(fxbug.dev/104887): Currently, not all display hardware supports the ability to
// set either the position or the alpha on a color layer, as color layers are not primary
// layers. There exist hardware that require a color layer to be the backmost layer and to be
// the size of the entire display. This means that for the time being, we must rely on GPU
// composition for solid color rects.
//
// There is the option of assigning a 1x1 image with the desired color to a standard image layer,
// as a way of mimicking color layers (and this is what is done in the GPU path as well) --
// however, not all hardware supports images with sizes that differ from the destination size of
// the rect. So implementing that solution on the display path as well is problematic.
#if 0

  auto [src, dst] = DisplaySrcDstFrames::New(rectangle, image);

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

  (*display_controller_.get())->SetLayerPrimaryPosition(layer_id, transform, src, dst);
  auto alpha_mode = GetAlphaMode(image.blend_mode);
  (*display_controller_.get())->SetLayerPrimaryAlpha(layer_id, alpha_mode, image.multiply_color[3]);
#endif
}

void DisplayCompositor::ApplyLayerImage(uint32_t layer_id, escher::Rectangle2D rectangle,
                                        allocation::ImageMetadata image,
                                        scenic_impl::DisplayEventId wait_id,
                                        scenic_impl::DisplayEventId signal_id) {
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
      .width = image.width,
      .height = image.height,
      .pixel_format = BufferCollectionPixelFormatToZirconFormat(pixel_format),
      .type = BufferCollectionPixelFormatToImageType(pixel_format)};

  (*display_controller_.get())->SetLayerPrimaryConfig(layer_id, image_config);

  FX_DCHECK(src.width && src.height) << "Source frame cannot be empty.";
  FX_DCHECK(dst.width && dst.height) << "Destination frame cannot be empty.";
  (*display_controller_.get())->SetLayerPrimaryPosition(layer_id, transform, src, dst);

  auto alpha_mode = GetAlphaMode(image.blend_mode);
  (*display_controller_.get())->SetLayerPrimaryAlpha(layer_id, alpha_mode, image.multiply_color[3]);

  // Set the imported image on the layer.
  (*display_controller_.get())->SetLayerImage(layer_id, image.identifier, wait_id, signal_id);
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
  pending_images_in_config_.clear();
  fuchsia::hardware::display::ConfigResult result;
  std::vector<fuchsia::hardware::display::ClientCompositionOp> ops;
  std::unique_lock<std::mutex> lock(lock_);
  (*display_controller_.get())->CheckConfig(/*discard*/ true, &result, &ops);
}

fuchsia::hardware::display::ConfigStamp DisplayCompositor::ApplyConfig() {
  TRACE_DURATION("gfx", "flatland::DisplayCompositor::ApplyConfig");
  std::unique_lock<std::mutex> lock(lock_);
  auto status = (*display_controller_.get())->ApplyConfig();
  FX_DCHECK(status == ZX_OK);
  fuchsia::hardware::display::ConfigStamp pending_config_stamp;
  status = (*display_controller_.get())->GetLatestAppliedConfigStamp(&pending_config_stamp);
  FX_DCHECK(status == ZX_OK);
  return pending_config_stamp;
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
  // layers, then we fall back to software composition.
  bool hardware_fail = false;
  if (!kDisableDisplayComposition) {
    for (auto& data : render_data_list) {
      if (!SetRenderDataOnDisplay(data)) {
        // TODO(fxbug.dev/77416): just because setting the data on one display fails (e.g. due to
        // too many layers), that doesn't mean that all displays need to use GPU-composition.  Some
        // day we might want to use GPU-composition for some client images, and direct-scanout for
        // others.
        hardware_fail = true;
        break;
      }
      if (should_apply_display_color_conversion_) {
        // Apply direct-to-display color conversion here.
        zx_status_t status = (*display_controller_)
                                 ->SetDisplayColorConversion(
                                     data.display_id, color_conversion_preoffsets_,
                                     color_conversion_coefficients_, color_conversion_postoffsets_);
        FX_CHECK(status == ZX_OK) << "Could not apply hardware color conversion: " << status;
      }
    }
  }

  // Determine whether we need to fall back to GPU composition.  Avoid calling CheckConfig() if we
  // don't need to, because this requires a round-trip to the display controller.
  bool fallback_to_gpu_composition = false;
  if (hardware_fail || kDisableDisplayComposition) {
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
      if (display_engine_data.vmo_count == 0) {
        FX_LOGS(WARNING) << "No VMOs were created when creating display.";
        return;
      }
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

      // TODO(fxbug.dev/91737): Remove this after the direct-to-display path is stable.
      // We expect the retired event to already have been signaled. Verify this without waiting.
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
    // Unsignal image events before applying config.
    for (auto id : pending_images_in_config_) {
      image_event_map_[id].signal_event.signal(ZX_EVENT_SIGNALED, 0);
    }

    // See ReleaseFenceManager comments for details.
    release_fence_manager_.OnDirectScanoutFrame(frame_number, std::move(release_fences),
                                                std::move(callback));
  }

  // TODO(fxbug.dev/77414): we should be calling ApplyConfig2() here, but it's not implemented yet.
  // Additionally, if the previous frame was "direct scanout" (but not if "gpu composited") we
  // should obtain the fences for that frame and pass them directly to ApplyConfig2().
  // ReleaseFenceManager is somewhat poorly suited to this, because it was designed for an old
  // version of ApplyConfig2(), which latter proved to be infeasible for some drivers to implement.
  const auto& config_stamp = ApplyConfig();
  pending_apply_configs_.push_back({.config_stamp = config_stamp, .frame_number = frame_number});
}

void DisplayCompositor::OnVsync(zx::time timestamp,
                                fuchsia::hardware::display::ConfigStamp applied_config_stamp) {
  TRACE_DURATION("gfx", "Flatland::DisplayCompositor::OnVsync");

  // We might receive multiple OnVsync() callbacks with the same |applied_config_stamp| if the scene
  // doesn't change. Early exit for these cases.
  if (last_presented_config_stamp_.has_value() &&
      fidl::Equals(applied_config_stamp, last_presented_config_stamp_.value())) {
    return;
  }

  // Verify that the configuration from Vsync is in the [pending_apply_configs_] queue.
  auto vsync_frame_it = std::find_if(pending_apply_configs_.begin(), pending_apply_configs_.end(),
                                     [applied_config_stamp](const ApplyConfigInfo& info) {
                                       return fidl::Equals(info.config_stamp, applied_config_stamp);
                                     });

  // It is possible that the config stamp doesn't match any config applied by this DisplayCompositor
  // instance. i.e. it could be from another client. Thus we just ignore these events.
  if (vsync_frame_it == pending_apply_configs_.end()) {
    FX_LOGS(INFO) << "The config stamp <" << applied_config_stamp.value << "> was not generated "
                  << "by current DisplayCompositor. Vsync event skipped.";
    return;
  }

  // Handle the presented ApplyConfig() call, as well as the skipped ones.
  auto it = pending_apply_configs_.begin();
  auto end = std::next(vsync_frame_it);
  while (it != end) {
    release_fence_manager_.OnVsync(it->frame_number, timestamp);
    it = pending_apply_configs_.erase(it);
  }
  last_presented_config_stamp_ = applied_config_stamp;
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

DisplayCompositor::ImageEventData DisplayCompositor::NewImageEventData() {
  ImageEventData result;

  std::unique_lock<std::mutex> lock(lock_);

  // The DC signals this once it has set the layer image.  We pre-signal this event so the first
  // frame rendered with it behaves as though it was previously OKed for recycling.
  auto status = zx::event::create(0, &result.signal_event);
  FX_DCHECK(status == ZX_OK);
  status = result.signal_event.signal(0, ZX_EVENT_SIGNALED);
  FX_DCHECK(status == ZX_OK);
  result.signal_id = scenic_impl::ImportEvent(*display_controller_.get(), result.signal_event);
  FX_DCHECK(result.signal_id != fuchsia::hardware::display::INVALID_DISP_ID);

  return result;
}

allocation::GlobalBufferCollectionId DisplayCompositor::AddDisplay(
    scenic_impl::display::Display* display, DisplayInfo info, uint32_t num_vmos,
    fuchsia::sysmem::BufferCollectionInfo_2* out_collection_info) {
  const auto display_id = display->display_id();
  FX_DCHECK(display_engine_data_map_.find(display_id) == display_engine_data_map_.end())
      << "DisplayCompositor::AddDisplay(): display already exists: " << display_id;

  const uint32_t width = info.dimensions.x;
  const uint32_t height = info.dimensions.y;

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

  // Add vsync callback on display. Note that this will overwrite the existing callback on
  // |display| and other clients won't receive any, i.e. gfx.
  display->SetVsyncCallback(
      [weak_ref = weak_from_this()](zx::time timestamp,
                                    fuchsia::hardware::display::ConfigStamp applied_config_stamp) {
        if (auto ref = weak_ref.lock())
          ref->OnVsync(timestamp, applied_config_stamp);
      });

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
  auto result = renderer_->ImportBufferCollection(
      collection_id, sysmem_allocator_.get(), std::move(renderer_token),
      BufferCollectionUsage::kRenderTarget, std::optional<fuchsia::math::SizeU>({width, height}));
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
          sysmem_allocator_.get(), std::move(compositor_token), num_vmos, width, height,
          buffer_usage, ConvertZirconFormatToSysmemFormat(pixel_format), memory_constraints);
#else
  fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.min_buffer_count_for_camping = num_vmos;
  constraints.usage.none = fuchsia::sysmem::noneUsage;

  fuchsia::sysmem::BufferCollectionSyncPtr collection_ptr;
  status = sysmem_allocator_->BindSharedCollection(std::move(compositor_token),
                                                   collection_ptr.NewRequest());
  collection_ptr->SetName(10u, "FlatlandDisplayCompositorImage");
  status = collection_ptr->SetConstraints(true, constraints);
  FX_DCHECK(status == ZX_OK);
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

  // We know that this collection is supported by display because we collected constraints from
  // display in scenic_impl::ImportBufferCollection() and waited for successful allocation.
  buffer_collection_supports_display_[collection_id] = true;
  buffer_collection_pixel_format_[collection_id] =
      out_collection_info->settings.image_format_constraints.pixel_format;

  // Import the images as well.
  for (uint32_t i = 0; i < num_vmos; i++) {
    allocation::ImageMetadata target = {.collection_id = collection_id,
                                        .identifier = allocation::GenerateUniqueImageId(),
                                        .vmo_index = i,
                                        .width = width,
                                        .height = height};
    display_engine_data.frame_event_datas.push_back(NewFrameEventData());
    display_engine_data.targets.push_back(target);
    bool res = ImportBufferImage(target, BufferCollectionUsage::kRenderTarget);
    FX_DCHECK(res);
  }

  display_engine_data.vmo_count = num_vmos;
  display_engine_data.curr_vmo = 0;
  return collection_id;
}

void DisplayCompositor::SetColorConversionValues(const std::array<float, 9>& coefficients,
                                                 const std::array<float, 3>& preoffsets,
                                                 const std::array<float, 3>& postoffsets) {
  // Lock the whole function.
  std::unique_lock<std::mutex> lock(lock_);
  color_conversion_coefficients_ = coefficients;
  color_conversion_preoffsets_ = preoffsets;
  color_conversion_postoffsets_ = postoffsets;
  should_apply_display_color_conversion_ = (coefficients != kDefaultColorConversionCoefficients) ||
                                           (preoffsets != kDefaultColorConversionOffsets) ||
                                           (postoffsets != kDefaultColorConversionOffsets);
  renderer_->SetColorConversionValues(coefficients, preoffsets, postoffsets);
}

bool DisplayCompositor::SetMinimumRgb(uint8_t minimum_rgb) {
  fuchsia::hardware::display::Controller_SetMinimumRgb_Result cmd_result;
  auto status = (*display_controller_)->SetMinimumRgb(minimum_rgb, &cmd_result);
  if (status != ZX_OK || cmd_result.is_err()) {
    FX_LOGS(WARNING) << "FlatlandDisplayCompositor SetMinimumRGB failed";
    return false;
  }
  return true;
}

}  // namespace flatland
