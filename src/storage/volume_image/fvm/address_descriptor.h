// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_FVM_ADDRESS_DESCRIPTOR_H_
#define SRC_STORAGE_VOLUME_IMAGE_FVM_ADDRESS_DESCRIPTOR_H_

#include <lib/fit/result.h>

#include <cstdint>
#include <vector>

#include <fbl/span.h>

namespace storage::volume_image {

struct AddressSpace {
  // Where the address space begins.
  uint64_t offset;

  // Number of addressable blocks in this address space.
  uint64_t count;
};

// Describes a mapping from an address space in a source format, into a target space.
// The target space is the fvm virtual address space that each partition has.
struct AddressMap {
  // Original address space, where data is read from.
  AddressSpace source;

  // Target address space, where data is written to, in the fvm image.
  AddressSpace target;
};

// Represents how the input partition image, should be transformed to fit in the fvm.
struct AddressDescriptor {
  // Returns an |AddressDescriptor| containing the deserialized contents from |serialized|.
  //
  // On error, returns a string describing the error condition.
  static fit::result<AddressDescriptor, std::string> Deserialize(fbl::Span<uint8_t> serialized);

  // Returns a vector containing a serialized version of |this|.
  //
  // On error, returns a string describing the error condition.
  fit::result<std::vector<uint8_t>, std::string> Serialize();

  // List of mappings.
  std::vector<AddressMap> mappings;
};

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_FVM_ADDRESS_DESCRIPTOR_H_
