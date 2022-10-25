// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "screen_capture.h"

#include <lib/fpromise/result.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/syscalls.h>

#include <utility>

#include "src/lib/fsl/handles/object_info.h"
#include "src/ui/scenic/lib/allocation/buffer_collection_importer.h"
#include "src/ui/scenic/lib/flatland/engine/engine.h"
#include "src/ui/scenic/lib/flatland/renderer/renderer.h"

using flatland::ImageRect;
using fuchsia::ui::composition::FrameInfo;
using fuchsia::ui::composition::Orientation;
using fuchsia::ui::composition::ScreenCaptureConfig;
using fuchsia::ui::composition::ScreenCaptureError;
using std::vector;

namespace {

// The number of orientations in |fuchsia.ui.composition.Orientation|.
constexpr int kNumOrientations = 4;

Orientation GetNewOrientation(Orientation screen_capture_rotation, Orientation prev_orientation) {
  // Orientation values are an enum with an uint value in the range [1, 4], where value 1 represents
  // no rotation and each subsequent value is a (pi/2) rotation such that value 4 represents a
  // (3pi/2) rotation, or, (-pi/2) rotation.
  int a = static_cast<int>(screen_capture_rotation) - 1;
  int b = static_cast<int>(prev_orientation) - 1;

  return static_cast<Orientation>(((a + b) % kNumOrientations) + 1);
}

}  // namespace

namespace screen_capture {

ScreenCapture::ScreenCapture(
    fidl::InterfaceRequest<fuchsia::ui::composition::ScreenCapture> request,
    const vector<std::shared_ptr<allocation::BufferCollectionImporter>>&
        buffer_collection_importers,
    std::shared_ptr<flatland::Renderer> renderer, GetRenderables get_renderables)
    : binding_(this, std::move(request)),
      buffer_collection_importers_(buffer_collection_importers),
      renderer_(std::move(renderer)),
      get_renderables_(std::move(get_renderables)) {
  binding_.set_error_handler([this](zx_status_t status) { ClearImages(); });
}

ScreenCapture::~ScreenCapture() { ClearImages(); }

void ScreenCapture::Configure(ScreenCaptureConfig args, ConfigureCallback callback) {
  // Check for missing args.
  if (!args.has_import_token() || !args.has_size() || !args.size().width || !args.size().height ||
      !args.has_buffer_count()) {
    FX_LOGS(WARNING) << "ScreenCapture::Configure: Missing arguments.";
    callback(fpromise::error(ScreenCaptureError::MISSING_ARGS));
    return;
  }

  // Check for invalid args.
  if (args.buffer_count() < 1) {
    FX_LOGS(WARNING) << "ScreenCapture::Configure: There must be at least one buffer.";
    callback(fpromise::error(ScreenCaptureError::INVALID_ARGS));
    return;
  }

  auto import_token = args.mutable_import_token();
  const zx_koid_t global_collection_id = fsl::GetRelatedKoid(import_token->value.get());

  // Event pair ID must be valid.
  if (global_collection_id == ZX_KOID_INVALID) {
    FX_LOGS(WARNING) << "ScreenCapture::Configure: Event pair ID must be valid.";
    callback(fpromise::error(ScreenCaptureError::INVALID_ARGS));
    return;
  }

  // Release any existing buffers and reset image_ids_ and available_buffers_
  ClearImages();

  // Create the associated metadata. Note that clients are responsible for ensuring reasonable
  // parameters.
  allocation::ImageMetadata metadata;
  metadata.collection_id = global_collection_id;
  metadata.width = args.size().width;
  metadata.height = args.size().height;

  stream_rotation_ =
      args.has_rotation() ? args.rotation() : fuchsia::ui::composition::Rotation::CW_0_DEGREES;

  // For each buffer in the collection, add the image to our importers.
  for (uint32_t i = 0; i < args.buffer_count(); i++) {
    metadata.identifier = allocation::GenerateUniqueImageId();
    metadata.vmo_index = i;
    for (uint32_t j = 0; j < buffer_collection_importers_.size(); j++) {
      auto& importer = buffer_collection_importers_[j];
      auto result =
          importer->ImportBufferImage(metadata, allocation::BufferCollectionUsage::kRenderTarget);
      if (!result) {
        // If this importer fails, we need to release the image from all of the importers
        // that successfully imported it and release all of the past buffer images as well.
        // Luckily we can do this right here instead of waiting for a fence since we know
        // these images are not being used by anything yet.
        for (uint32_t k = 0; k < j; k++) {
          buffer_collection_importers_[k]->ReleaseBufferImage(metadata.identifier);
        }
        ClearImages();

        FX_LOGS(WARNING) << "ScreenCapture::Configure: Failed to import BufferImage.";
        callback(fpromise::error(ScreenCaptureError::BAD_OPERATION));
        return;
      }
    }
    image_ids_[i] = metadata;
    available_buffers_.push_back(i);
  }
  // Everything was successful!
  callback(fpromise::ok());
}

void ScreenCapture::GetNextFrame(fuchsia::ui::composition::GetNextFrameArgs args,
                                 ScreenCapture::GetNextFrameCallback callback) {
  // Check that we have an available buffer that we can render.
  if (available_buffers_.empty()) {
    if (image_ids_.empty()) {
      FX_LOGS(ERROR)
          << "ScreenCapture::GetNextFrame: No buffers configured. Was Configure called previously?";
      callback(fpromise::error(ScreenCaptureError::BAD_OPERATION));
      return;
    }
    FX_LOGS(WARNING) << "ScreenCapture::GetNextFrame: No buffers available.";
    callback(fpromise::error(ScreenCaptureError::BUFFER_FULL));
    return;
  }

  if (!args.has_event()) {
    FX_LOGS(WARNING) << "ScreenCapture::GetNextFrame: Missing arguments.";
    callback(fpromise::error(ScreenCaptureError::MISSING_ARGS));
    return;
  }

  // Get renderables from the engine.
  // TODO(fxbug.dev/97057): Ensure this does not happen more than once in the same vsync.
  auto renderables = get_renderables_();
  const auto& rects = renderables.first;
  const auto& image_metadatas = renderables.second;

  uint32_t buffer_id = available_buffers_.front();
  const auto& metadata = image_ids_[buffer_id];

  auto image_width = metadata.width;
  auto image_height = metadata.height;

  const auto& rotated_rects = RotateRenderables(rects, stream_rotation_, image_width, image_height);

  std::vector<zx::event> events;
  events.emplace_back(args.mutable_event()->release());

  // Render content into user-provided buffer, which will signal the user-provided event.
  renderer_->Render(metadata, rotated_rects, image_metadatas, events);

  FrameInfo frame_info;
  frame_info.set_buffer_id(buffer_id);

  available_buffers_.pop_front();
  callback(fpromise::ok(std::move(frame_info)));
}

void ScreenCapture::ReleaseFrame(uint32_t buffer_id, ReleaseFrameCallback callback) {
  // Check that the buffer index is in range.
  if (image_ids_.find(buffer_id) == image_ids_.end()) {
    FX_LOGS(WARNING) << "ScreenCapture::ReleaseFrame: Buffer ID does not exist.";
    callback(fpromise::error(ScreenCaptureError::INVALID_ARGS));
    return;
  }

  // Check that the buffer index is not already available.
  if (find(available_buffers_.begin(), available_buffers_.end(), buffer_id) !=
      available_buffers_.end()) {
    FX_LOGS(WARNING) << "ScreenCapture::ReleaseFrame: Buffer ID already available.";
    callback(fpromise::error(ScreenCaptureError::INVALID_ARGS));
    return;
  }

  available_buffers_.push_back(buffer_id);
  callback(fpromise::ok());
}

void ScreenCapture::ClearImages() {
  for (auto& image_id : image_ids_) {
    auto identifier = image_id.second.identifier;
    for (auto& buffer_collection_importer : buffer_collection_importers_) {
      buffer_collection_importer->ReleaseBufferImage(identifier);
    }
  }
  image_ids_.clear();
  available_buffers_.clear();
}

std::vector<ImageRect> ScreenCapture::RotateRenderables(const std::vector<ImageRect>& rects,
                                                        fuchsia::ui::composition::Rotation rotation,
                                                        uint32_t image_width,
                                                        uint32_t image_height) {
  if (rotation == fuchsia::ui::composition::Rotation::CW_0_DEGREES)
    return rects;

  std::vector<ImageRect> final_rects;

  for (const auto& rect : rects) {
    auto origin = rect.origin;
    auto extent = rect.extent;
    auto texel_uvs = rect.texel_uvs;
    auto orientation = rect.orientation;

    // (x,y) is the origin pre-rotation. (0,0) is the top-left of the image.
    auto x = origin[0];
    auto y = origin[1];

    // (w, h) is the width and height of the rectangle pre-rotation.
    auto w = extent[0];
    auto h = extent[1];

    // Account for translation of the rectangle in the bounds of the canvas.
    vec2 new_origin;
    // Account for the new extent.
    vec2 new_extent;
    // Account for the new orientation.
    Orientation new_orientation;

    switch (rotation) {
      case fuchsia::ui::composition::Rotation::CW_90_DEGREES:
        new_origin = {static_cast<float>(image_width) - y - h, x};
        new_extent = {h, w};
        // The renderer requires counter-clockwise rotation instead of clockwise as used by screen
        // capture. 90 clockwise is equivalent to 270 counter-clockwise.
        new_orientation = GetNewOrientation(Orientation::CCW_270_DEGREES, orientation);
        break;
      case fuchsia::ui::composition::Rotation::CW_180_DEGREES:
        new_origin = {static_cast<float>(image_width) - x - w,
                      static_cast<float>(image_height) - y - h};
        new_extent = {w, h};
        new_orientation = GetNewOrientation(Orientation::CCW_180_DEGREES, orientation);
        break;
      case fuchsia::ui::composition::Rotation::CW_270_DEGREES:
        new_origin = {y, static_cast<float>(image_height) - x - w};
        new_extent = {h, w};
        // The renderer requires counter-clockwise rotation instead of clockwise as used by screen
        // capture. 270 clockwise is equivalent to 90 counter-clockwise.
        new_orientation = GetNewOrientation(Orientation::CCW_90_DEGREES, orientation);
        break;
      default:
        FX_DCHECK(false);
        break;
    }

    final_rects.emplace_back(new_origin, new_extent, texel_uvs, new_orientation);
  }

  return final_rects;
}

}  // namespace screen_capture
