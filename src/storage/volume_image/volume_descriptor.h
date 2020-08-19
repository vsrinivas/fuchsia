// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_VOLUME_DESCRIPTOR_H_
#define SRC_STORAGE_VOLUME_IMAGE_VOLUME_DESCRIPTOR_H_

#include <lib/fit/result.h>

#include <array>
#include <cstdint>
#include <unordered_set>
#include <vector>

#include "src/storage/volume_image/options.h"
#include "src/storage/volume_image/utils/guid.h"

namespace storage::volume_image {

constexpr uint64_t kNameLength = 40;

// Metadata describing the block image to be generated.
struct VolumeDescriptor {
  static constexpr uint64_t kMagic = 0xB10C14;

  // On success returns the VolumeDescriptor with the deserialized contents of |serialized|.
  static fit::result<VolumeDescriptor, std::string> Deserialize(
      fbl::Span<const uint8_t> serialized);

  // On success returns the VolumeDescriptor with the deserialized contents of |serialized|.
  static fit::result<VolumeDescriptor, std::string> Deserialize(fbl::Span<const char> serialized) {
    return Deserialize(fbl::Span<const uint8_t>(reinterpret_cast<const uint8_t*>(serialized.data()),
                                                serialized.size() * sizeof(char)));
  }

  // Returns a byte vector containing the serialized version data.
  // The serialization is meant to be human readable.
  fit::result<std::vector<uint8_t>, std::string> Serialize() const;

  // Instance Guid expected for the partition.
  std::array<uint8_t, kGuidLength> instance = {};

  // Type Guid expected for the partition.
  std::array<uint8_t, kGuidLength> type = {};

  // Name expected for the partition.
  std::string name;

  // Number of bytes used to chunk the image.
  uint64_t block_size = 0;

  // Encryption options for this image.
  EncryptionType encryption = EncryptionType::kNone;

  // Arbitrary options to tweak the tools behavior for the respective image.
  std::unordered_set<Option> options;

  // The target size of the device/partition that this image is destined for.
  uint64_t size = 0;
};

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_VOLUME_DESCRIPTOR_H_
