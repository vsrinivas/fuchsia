// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/adapter/empty_partition.h"

#include <limits>

#include "src/storage/fvm/format.h"
#include "src/storage/volume_image/address_descriptor.h"
#include "src/storage/volume_image/options.h"
#include "src/storage/volume_image/partition.h"
#include "src/storage/volume_image/utils/reader.h"
#include "src/storage/volume_image/volume_descriptor.h"

namespace storage::volume_image {
namespace {

class DummyReader final : public Reader {
 public:
  uint64_t length() const final { return std::numeric_limits<uint64_t>::max(); }

  fpromise::result<void, std::string> Read(uint64_t offset,
                                           cpp20::span<uint8_t> buffer) const final {
    return fpromise::ok();
  }
};

}  // namespace

fpromise::result<Partition, std::string> CreateEmptyFvmPartition(
    const PartitionOptions& partition_options, const FvmOptions& fvm_options) {
  if (partition_options.max_bytes.value_or(0) == 0) {
    return fpromise::error("Must provide a non-zero size for empty partition.");
  }

  if (fvm_options.slice_size == 0) {
    return fpromise::error("must provide a non-zero slice size.");
  }

  VolumeDescriptor descriptor;
  descriptor.block_size = 8192;
  descriptor.instance = fvm::kPlaceHolderInstanceGuid;
  descriptor.size = partition_options.max_bytes.value();

  AddressMap mapping;
  mapping.source = 0;
  mapping.target = 0;
  mapping.count = 0;
  mapping.size = descriptor.size;
  mapping.options[EnumAsString(AddressMapOption::kFill)] = 0;

  AddressDescriptor address;
  address.mappings.push_back(mapping);

  return fpromise::ok(Partition(descriptor, address, std::make_unique<DummyReader>()));
}

}  // namespace storage::volume_image
