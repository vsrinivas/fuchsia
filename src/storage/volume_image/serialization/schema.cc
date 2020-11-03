// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/serialization/schema.h"

#include <fstream>
#include <iterator>
#include <streambuf>
#include <string>

#include "src/storage/volume_image/utils/path.h"

namespace storage::volume_image {

// Path to where the schema leaves. Left to be injected by a compile time definition.
static constexpr char kPath[] = STORAGE_VOLUME_IMAGE_SCHEMA_PATH;

std::string GetSchema(Schema schema) {
  std::ifstream file;
  std::string base_path = GetBasePath();

  switch (schema) {
    case Schema::kVolumeDescriptor:
      file.open(base_path.append(kPath).append("volume_descriptor.schema.json"));
      return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    case Schema::kAddressDescriptor:
      file.open(base_path.append(kPath).append("address_descriptor.schema.json"));
      return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    case Schema::kVolumeImage:
      file.open(base_path.append(kPath).append("volume_image.schema.json"));
      return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    case Schema::kBlobManifest:
      file.open(base_path.append(kPath).append("blob_manifest.schema.json"));
      return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    default:
      return std::string();
  }
}

}  // namespace storage::volume_image
