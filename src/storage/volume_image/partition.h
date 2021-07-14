// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_PARTITION_H_
#define SRC_STORAGE_VOLUME_IMAGE_PARTITION_H_

#include <lib/fpromise/result.h>

#include <memory>
#include <string>

#include "src/storage/volume_image/address_descriptor.h"
#include "src/storage/volume_image/utils/reader.h"
#include "src/storage/volume_image/volume_descriptor.h"

namespace storage::volume_image {

// A Partition consists of the volume descriptor, allowing the fvm to know how the partition should
// look, an address descriptor allowing the fvm to know how the volume data should be moved in the
// fvm address space and last a reader, which provides access to the volume data in the volume
// address space.
//
// This class is move constructible and move assignable only.
// This class is thread-compatible.
class Partition {
 public:
  // Defines a strict and unique ordering between unique partitions.
  //
  // Partitions are first ordered lexicographically by name, and then instance GUID.
  struct LessThan {
    // Returns true if |lhs| partition should be before |rhs| partition.
    bool operator()(const Partition& lhs, const Partition& rhs) const;
  };

  // On success returns a Partition representing the serialized volume image, which contains the
  // volume and address descriptors, and backed by |reader|. On error retruns a string describing
  // the failure reason.
  static fpromise::result<Partition, std::string> Create(std::string_view serialized_volume_image,
                                                         std::unique_ptr<Reader> reader);

  Partition() = default;
  Partition(VolumeDescriptor volume_descriptor, AddressDescriptor address_descriptor,
            std::unique_ptr<Reader> reader)
      : volume_(std::move(volume_descriptor)),
        address_(std::move(address_descriptor)),
        reader_(std::move(reader)) {}
  Partition(const Partition&) = delete;
  Partition(Partition&&) noexcept = default;
  Partition& operator=(const Partition&) = delete;
  Partition& operator=(Partition&&) noexcept = default;
  ~Partition() = default;

  // Returns the volume descriptor for this partition.
  const VolumeDescriptor& volume() const { return volume_; }
  VolumeDescriptor& volume() { return volume_; }

  // Returns the address descriptor for this partition.
  const AddressDescriptor& address() const { return address_; }

  // Returns the reader for this partition, which allows reading the volume data from the source
  // address space.
  const Reader* reader() const { return reader_.get(); }

 private:
  // Information about the volume in this partition.
  VolumeDescriptor volume_;

  // Information about the address or extents in this partitions and how to map them to target
  // space.
  AddressDescriptor address_;

  // Mechanism for reading volume data.
  std::unique_ptr<Reader> reader_ = nullptr;
};

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_PARTITION_H_
