// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_ADDRESS_DESCRIPTOR_H_
#define SRC_STORAGE_VOLUME_IMAGE_ADDRESS_DESCRIPTOR_H_

#include <lib/fpromise/result.h>
#include <lib/stdcompat/span.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace storage::volume_image {

// Describes a mapping from an address space in a source format, into a target space.
// The target space is the address space that each volume expects to see.
struct AddressMap {
  // Returns a human readable string.
  std::string DebugString() const;

  // Original address space, where data is read from.
  uint64_t source = 0;

  // Target address space, where data is written to, in the target volume address space.
  uint64_t target = 0;

  // Number of addressable bytes in this address space to be written to the image.
  uint64_t count = 0;

  // Number of bytes that are expected in this mapping.
  // This allows initializing arbitrary big mappings, with only |count| bytes.
  //  * If unset, |count| is treated as size.
  //  * If set, and lower than |count|, |count| is picked as size.
  std::optional<uint64_t> size;

  // Options that apply to this mapping.
  std::map<std::string, uint64_t> options;
};

// Represents how the input partition image, should be transformed to fit in the image.
struct AddressDescriptor {
  static constexpr uint64_t kMagic = 0xADD835DE5C817085;

  // Returns an |AddressDescriptor| containing the deserialized contents from |serialized|.
  //
  // On error, returns a string describing the error condition.
  static fpromise::result<AddressDescriptor, std::string> Deserialize(
      cpp20::span<const uint8_t> serialized);

  // On success returns the VolumeDescriptor with the deserialized contents of |serialized|.
  static fpromise::result<AddressDescriptor, std::string> Deserialize(
      cpp20::span<const char> serialized) {
    return Deserialize(cpp20::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(serialized.data()), serialized.size() * sizeof(char)));
  }

  // Returns a vector containing a serialized version of |this|.
  //
  // On error, returns a string describing the error condition.
  fpromise::result<std::vector<uint8_t>, std::string> Serialize() const;

  // List of mappings.
  std::vector<AddressMap> mappings;
};

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_ADDRESS_DESCRIPTOR_H_
