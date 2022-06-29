// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "screen_capture2.h"

#include <lib/syslog/cpp/macros.h>

#include <utility>

#include "src/lib/fsl/handles/object_info.h"

using fuchsia::ui::composition::internal::ScreenCaptureConfig;
using fuchsia::ui::composition::internal::ScreenCaptureError;
using std::vector;

namespace screen_capture2 {

ScreenCapture::ScreenCapture(
    fidl::InterfaceRequest<fuchsia::ui::composition::internal::ScreenCapture> request,
    std::shared_ptr<screen_capture::ScreenCaptureBufferCollectionImporter>
        screen_capture_buffer_collection_importer)
    : binding_(this, std::move(request)),
      screen_capture_buffer_collection_importer_(
          std::move(screen_capture_buffer_collection_importer)) {}

ScreenCapture::~ScreenCapture() { ClearImages(); }

void ScreenCapture::Configure(ScreenCaptureConfig args, ConfigureCallback callback) {
  if (!args.has_image_size()) {
    FX_LOGS(WARNING) << "ScreenCapture::Configure: Missing image size";
    callback(fpromise::error(ScreenCaptureError::MISSING_ARGS));
    return;
  }

  if (!args.has_import_token()) {
    FX_LOGS(WARNING) << "ScreenCapture::Configure: Missing import token";
    callback(fpromise::error(ScreenCaptureError::MISSING_ARGS));
    return;
  }

  if (!args.image_size().width || !args.image_size().height) {
    FX_LOGS(WARNING) << "ScreenCapture::Configure: Invalid arguments.";
    callback(fpromise::error(ScreenCaptureError::INVALID_ARGS));
    return;
  }

  auto import_token = args.mutable_import_token();
  const zx_koid_t global_collection_id = fsl::GetRelatedKoid(import_token->value.get());

  if (global_collection_id == ZX_KOID_INVALID) {
    FX_LOGS(WARNING) << "ScreenCapture::Configure: Event pair ID must be valid.";
    callback(fpromise::error(ScreenCaptureError::INVALID_ARGS));
    return;
  }

  std::optional<BufferCount> buffer_count_opt =
      screen_capture_buffer_collection_importer_->GetBufferCollectionBufferCount(
          global_collection_id);
  if (!buffer_count_opt) {
    FX_LOGS(WARNING) << "ScreenCapture::Configure: Failed to get BufferCount.";
    callback(fpromise::error(ScreenCaptureError::INVALID_ARGS));
    return;
  }

  BufferCount buffer_count = buffer_count_opt.value();

  if (buffer_count < 0) {
    FX_LOGS(WARNING) << "ScreenCapture::Configure: There must be at least 0 buffers.";
    callback(fpromise::error(ScreenCaptureError::INVALID_ARGS));
    return;
  }

  // Release any existing buffers and reset |image_ids_| and |available_buffers_|.
  ClearImages();

  // Create the associated metadata. Note that clients are responsible for ensuring reasonable
  // parameters.
  allocation::ImageMetadata metadata;
  metadata.collection_id = global_collection_id;
  metadata.width = args.image_size().width;
  metadata.height = args.image_size().height;

  // For each buffer in the collection, add the image to the importer.
  for (uint32_t i = 0; i < buffer_count; i++) {
    metadata.identifier = allocation::GenerateUniqueImageId();
    metadata.vmo_index = i;
    auto result = screen_capture_buffer_collection_importer_->ImportBufferImage(metadata);
    if (!result) {
      ClearImages();

      FX_LOGS(WARNING) << "ScreenCapture::Configure: Failed to import BufferImage at index " << i;
      callback(fpromise::error(ScreenCaptureError::INVALID_ARGS));
      return;
    }
    image_ids_[i] = metadata;
    available_buffers_.push_back(i);
  }

  callback(fpromise::ok());
}

void ScreenCapture::GetNextFrame(ScreenCapture::GetNextFrameCallback callback) {
  FX_NOTIMPLEMENTED();
}

void ScreenCapture::ClearImages() {
  for (auto& image_id : image_ids_) {
    auto identifier = image_id.second.identifier;
    screen_capture_buffer_collection_importer_->ReleaseBufferImage(identifier);
  }
  image_ids_.clear();
  available_buffers_.clear();
}

}  // namespace screen_capture2
