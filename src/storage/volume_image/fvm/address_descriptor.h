// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_FVM_ADDRESS_DESCRIPTOR_H_
#define SRC_STORAGE_VOLUME_IMAGE_FVM_ADDRESS_DESCRIPTOR_H_

#include <lib/fit/result.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <fbl/span.h>

namespace storage::volume_image {

// Describes a mapping from an address space in a source format, into a target space.
// The target space is the fvm virtual address space that each partition has.
struct AddressMap {
  // Original address space, where data is read from.
  uint64_t source = 0;

  // Target address space, where data is written to, in the fvm image.
  uint64_t target = 0;

  // Number of addressable blocks in this address space to be written.
  uint64_t count = 0;

  // Options that apply to this mapping.
  std::map<std::string, uint64_t> options;
};

// Represents how the input partition image, should be transformed to fit in the fvm.
struct AddressDescriptor {
  static constexpr uint64_t kMagic = 0xADD835DE5C817085;

  // Returns an |AddressDescriptor| containing the deserialized contents from |serialized|.
  //
  // On error, returns a string describing the error condition.
  static fit::result<AddressDescriptor, std::string> Deserialize(
      fbl::Span<const uint8_t> serialized);

  // On success returns the VolumeDescriptor with the deserialized contents of |serialized|.
  static fit::result<AddressDescriptor, std::string> Deserialize(fbl::Span<const char> serialized) {
    return Deserialize(fbl::Span<const uint8_t>(reinterpret_cast<const uint8_t*>(serialized.data()),
                                                serialized.size() * sizeof(char)));
  }

  // Returns a vector containing a serialized version of |this|.
  //
  // On error, returns a string describing the error condition.
  fit::result<std::vector<uint8_t>, std::string> Serialize() const;

  // List of mappings.
  std::vector<AddressMap> mappings;
};

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_FVM_ADDRESS_DESCRIPTOR_H_
