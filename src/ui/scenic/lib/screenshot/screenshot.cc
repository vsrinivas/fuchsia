// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "screenshot.h"

#include <lib/syslog/cpp/macros.h>

#include "src/lib/fsl/handles/object_info.h"
#include "src/ui/scenic/lib/flatland/engine/engine.h"
#include "src/ui/scenic/lib/flatland/renderer/vk_renderer.h"

using fuchsia::ui::composition::ScreenshotError;

namespace screenshot {

Screenshot::Screenshot(fidl::InterfaceRequest<fuchsia::ui::composition::Screenshot> request,
                       uint32_t display_width, uint32_t display_height,
                       const std::vector<std::shared_ptr<allocation::BufferCollectionImporter>>&
                           buffer_collection_importers,
                       std::shared_ptr<flatland::VkRenderer> renderer,
                       GetRenderables get_renderables)
    : binding_(this, std::move(request)),
      display_width_(display_width),
      display_height_(display_height),
      buffer_collection_importers_(buffer_collection_importers),
      renderer_(renderer),
      get_renderables_(std::move(get_renderables)) {}

void Screenshot::CreateImage(fuchsia::ui::composition::CreateImageArgs args,
                             CreateImageCallback callback) {
  // Check for missing args.
  if (!args.has_image_id() || !args.has_import_token() || !args.has_vmo_index() ||
      !args.has_size() || !args.size().width || !args.size().height) {
    FX_LOGS(WARNING) << "Screenshot::CreateImage: Missing arguments.";
    callback(fpromise::error(ScreenshotError::MISSING_ARGS));
    return;
  }

  auto import_token = std::move(args.mutable_import_token());

  // Check for invalid args. Remember that screenshot is initialized on a per-client basis, so
  // image_ids are scoped to the client.

  if (args.image_id() == kInvalidId) {
    FX_LOGS(WARNING) << "Screenshot::CreateImage: Image ID must be valid.";
    callback(fpromise::error(ScreenshotError::BAD_OPERATION));
    return;
  }

  const zx_koid_t global_collection_id = fsl::GetRelatedKoid(import_token->value.get());

  // Event pair ID must be valid.
  if (global_collection_id == ZX_KOID_INVALID) {
    FX_LOGS(WARNING) << "Screenshot::CreateImage: Event pair ID must be valid.";
    callback(fpromise::error(ScreenshotError::BAD_OPERATION));
    return;
  }

  // Create the associated metadata. Note that clients are responsible for ensuring reasonable
  // parameters.
  allocation::ImageMetadata metadata;
  metadata.identifier = allocation::GenerateUniqueImageId();
  metadata.collection_id = global_collection_id;
  metadata.vmo_index = args.vmo_index();
  metadata.width = args.size().width;
  metadata.height = args.size().height;

  // Add the image to our importers.
  for (uint32_t i = 0; i < buffer_collection_importers_.size(); i++) {
    auto& importer = buffer_collection_importers_[i];

    auto result = importer->ImportBufferImage(metadata);
    if (!result) {
      // If this importer fails, we need to release the image from all of the importers that it
      // passed on. Luckily we can do this right here instead of waiting for a fence since we know
      // this image isn't being used by anything yet.
      for (uint32_t j = 0; j < i; j++) {
        buffer_collection_importers_[j]->ReleaseBufferImage(metadata.identifier);
      }

      FX_LOGS(WARNING) << "Screenshot::CreateImage: Failed to import BufferImage.";
      callback(fpromise::error(ScreenshotError::BAD_OPERATION));
      return;
    }
  }

  // Everything was successful! Add it to the set.
  image_ids_[args.image_id()] = metadata;
  callback(fpromise::ok());
}

void Screenshot::RemoveImage(fuchsia::ui::composition::RemoveImageArgs args,
                             RemoveImageCallback callback) {
  // Check for missing args.
  if (!args.has_image_id()) {
    FX_LOGS(WARNING) << "Screenshot::RemoveImage: Missing arguments.";
    callback(fpromise::error(ScreenshotError::MISSING_ARGS));
    return;
  }

  if (image_ids_.find(args.image_id()) == image_ids_.end()) {
    FX_LOGS(WARNING) << "Screenshot::RemoveImage: Image ID does not exist.";
    callback(fpromise::error(ScreenshotError::BAD_OPERATION));
    return;
  }

  auto identifier = image_ids_[args.image_id()].identifier;

  for (auto& buffer_collection_importer : buffer_collection_importers_) {
    buffer_collection_importer->ReleaseBufferImage(identifier);
  }

  image_ids_.erase(args.image_id());
}

void Screenshot::TakeScreenshot(fuchsia::ui::composition::TakeScreenshotArgs args,
                                TakeScreenshotCallback callback) {
  // Check for missing args.
  if (!args.has_image_id() || !args.has_event()) {
    FX_LOGS(WARNING) << "Screenshot::TakeScreenshot: Missing arguments.";
    callback(fpromise::error(ScreenshotError::MISSING_ARGS));
    return;
  }

  auto image_id = args.image_id();

  // Check if image id is present.
  if (image_ids_.find(image_id) == image_ids_.end()) {
    FX_LOGS(WARNING) << "Screenshot::TakeScreenshot: Not a valid image ID";
    callback(fpromise::error(ScreenshotError::BAD_OPERATION));
    return;
  }

  auto metadata = image_ids_[image_id];

  // Get renderables from the engine.
  auto renderables = get_renderables_();

  auto rotation =
      args.has_rotation() ? args.rotation() : fuchsia::ui::composition::Rotation::CW_0_DEGREES;
  auto rotated_images =
      RotateRenderables(renderables.first, rotation, display_width_, display_height_);

  zx::event event = std::move(*(args.mutable_event()));
  std::vector<zx::event> events;
  events.push_back(std::move(event));

  // Render content into user-provided buffer, which will signal the user-provided event.
  renderer_->Render(metadata, rotated_images, renderables.second, std::move(events));
}

std::vector<Rectangle2D> Screenshot::RotateRenderables(const std::vector<Rectangle2D>& rects,
                                                       fuchsia::ui::composition::Rotation rotation,
                                                       uint32_t image_width,
                                                       uint32_t image_height) {
  if (rotation == fuchsia::ui::composition::Rotation::CW_0_DEGREES)
    return rects;

  std::vector<Rectangle2D> final_rects;

  for (size_t i = 0; i < rects.size(); ++i) {
    auto origin = rects[i].origin;
    auto extent = rects[i].extent;
    auto uvs = rects[i].clockwise_uvs;

    // (x,y) is the origin pre-rotation. (0,0) is the top-left of the image.
    auto x = origin[0];
    auto y = origin[1];

    // (w, h) is the width and height of the rectangle pre-rotation.
    auto w = extent[0];
    auto h = extent[1];

    // Account for the new image size if the rotation is 90 or 270 degrees.
    auto new_extent = extent;
    if (rotation != fuchsia::ui::composition::Rotation::CW_180_DEGREES) {
      new_extent = {h, w};
    }

    // Account for rotation of the rectangle itself.
    std::array<vec2, 4> new_uv_coords;
    // Account for translation of the rectangle in the bounds of the canvas.
    vec2 new_origin;

    switch (rotation) {
      case fuchsia::ui::composition::Rotation::CW_90_DEGREES:
        new_uv_coords = {uvs[3], uvs[0], uvs[1], uvs[2]};
        new_origin = {image_width - y - h, x};
        break;
      case fuchsia::ui::composition::Rotation::CW_180_DEGREES:
        new_uv_coords = {uvs[2], uvs[3], uvs[0], uvs[1]};
        new_origin = {image_width - x - w, image_height - y - h};
        break;
      case fuchsia::ui::composition::Rotation::CW_270_DEGREES:
        new_uv_coords = {uvs[1], uvs[2], uvs[3], uvs[0]};
        new_origin = {y, image_height - x - w};
        break;
      default:
        FX_DCHECK(false);
        break;
    }

    final_rects.push_back(Rectangle2D(new_origin, new_extent, new_uv_coords));
  }

  return final_rects;
}

}  // namespace screenshot
