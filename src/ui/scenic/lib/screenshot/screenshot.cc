// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "screenshot.h"

#include <lib/syslog/cpp/macros.h>

#include "src/lib/fsl/handles/object_info.h"

using fuchsia::ui::composition::CreateImageError;

namespace screenshot {

Screenshot::Screenshot(fidl::InterfaceRequest<fuchsia::ui::composition::Screenshot> request,
                       const std::vector<std::shared_ptr<allocation::BufferCollectionImporter>>&
                           buffer_collection_importers)
    : binding_(this, std::move(request)),
      buffer_collection_importers_(buffer_collection_importers) {}

void Screenshot::CreateImage(fuchsia::ui::composition::CreateImageArgs args,
                             CreateImageCallback callback) {
  // Check for missing args.
  if (!args.has_image_id() || !args.has_import_token() || !args.has_vmo_index() ||
      !args.has_image_width() || !args.has_image_height()) {
    FX_LOGS(WARNING) << "Screenshot::CreateImage: Missing arguments.";
    callback(fpromise::error(CreateImageError::MISSING_ARGS));
    return;
  }

  auto import_token = std::move(args.mutable_import_token());

  // Check for invalid args. Remember that screenshot is initialized on a per-client basis, so
  // image_ids are scoped to the client.

  if (args.image_id() == kInvalidId) {
    FX_LOGS(WARNING) << "Screenshot::CreateImage: Image ID must be valid.";
    callback(fpromise::error(CreateImageError::BAD_OPERATION));
    return;
  }

  const zx_koid_t global_collection_id = fsl::GetRelatedKoid(import_token->value.get());

  // Event pair ID must be valid.
  if (global_collection_id == ZX_KOID_INVALID) {
    FX_LOGS(WARNING) << "Screenshot::CreateImage: Event pair ID must be valid.";
    callback(fpromise::error(CreateImageError::BAD_OPERATION));
    return;
  }

  // Create the associated metadata. Note that clients are responsible for ensuring reasonable
  // parameters.
  allocation::ImageMetadata metadata;
  metadata.identifier = allocation::GenerateUniqueImageId();
  metadata.collection_id = global_collection_id;
  metadata.vmo_index = args.vmo_index();
  metadata.width = args.image_width();
  metadata.height = args.image_height();

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
      callback(fpromise::error(CreateImageError::BAD_OPERATION));
      return;
    }
  }

  // Everything was successful! Add it to the set.
  image_id_set_.insert(args.image_id());
  callback(fpromise::ok());
}

}  // namespace screenshot
