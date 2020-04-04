// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/serialization/schema.h"

#include <fstream>
#include <iterator>
#include <streambuf>
#include <string>

namespace storage::volume_image {

// Path to where the schema leaves. Left to be injected by a compile time definition.
static constexpr char kPath[] = STORAGE_VOLUME_IMAGE_SCHEMA_PATH;

std::string GetSchema(Schema schema) {
  std::ifstream file;

  switch (schema) {
    case Schema::kVolumeDescriptor:
      file.open(std::string(kPath).append("volume_descriptor.schema.json"));
      return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    default:
      return std::string();
  }
}

}  // namespace storage::volume_image
