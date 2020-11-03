// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_SERIALIZATION_SCHEMA_H_
#define SRC_STORAGE_VOLUME_IMAGE_SERIALIZATION_SCHEMA_H_

#include <string>

namespace storage::volume_image {

// Defines available schemas.
enum Schema {
  kVolumeDescriptor,
  kAddressDescriptor,
  kVolumeImage,
  kBlobManifest,
};

// Returns a string with the respective schema.
//
// On error, returns an empty string.
std::string GetSchema(Schema schema);

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_SERIALIZATION_SCHEMA_H_
