// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_LIB_PAVER_DEVICE_PARTITIONER_H_
#define SRC_STORAGE_LIB_PAVER_DEVICE_PARTITIONER_H_

#include <fuchsia/paver/llcpp/fidl.h>
#include <lib/zx/channel.h>
#include <lib/zx/status.h>
#include <stdbool.h>
#include <zircon/types.h>

#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include <fbl/span.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>

#include "src/storage/lib/paver/partition-client.h"
#include "src/storage/lib/paver/paver-context.h"

namespace paver {

// Whether the device uses the new or legacy partition scheme.
enum class PartitionScheme { kNew, kLegacy };

enum class Partition {
  kUnknown,
  kBootloaderA,
  kBootloaderB,
  kBootloaderR,
  kZirconA,
  kZirconB,
  kZirconR,
  kVbMetaA,
  kVbMetaB,
  kVbMetaR,
  kAbrMeta,
  kFuchsiaVolumeManager,
};

const char* PartitionName(Partition partition, PartitionScheme scheme);

enum class Arch {
  kX64,
  kArm64,
};

// Operations on a specific partition take two identifiers, a partition type
// and a content type.
//
// The first is the conceptual partition type. This may not necessarily map 1:1
// with on-disk partitions. For example, some devices have multiple bootloader
// stages which live in different partitions on-disk but which would both have
// type kBootloader.
//
// The second is a device-specific string identifying the contents the caller
// wants to read/write. The backend uses this to decide which on-disk partitions
// to use and where the content lives in them. The default content type is
// null or empty.
struct PartitionSpec {
 public:
  // Creates a spec with the given partition and default (null) content type.
  explicit PartitionSpec(Partition partition) : PartitionSpec(partition, std::string_view()) {}

  // Creates a spec with the given partition and content type.
  PartitionSpec(Partition partition, std::string_view content_type)
      : partition(partition), content_type(content_type) {}

  // Returns a descriptive string for logging.
  //
  // Does not necessary match the on-disk partition name, just meant to
  // indicate the conceptual partition type in a device-agnostic way.
  fbl::String ToString() const;

  Partition partition;
  std::string_view content_type;
};

inline bool SpecMatches(const PartitionSpec& a, const PartitionSpec& b) {
  return a.partition == b.partition && a.content_type == b.content_type;
}

// Abstract device partitioner definition.
// This class defines common APIs for interacting with a device partitioner.
class DevicePartitioner {
 public:
  virtual ~DevicePartitioner() = default;

  // Whether or not the Fuchsia Volume Manager exists within an FTL.
  virtual bool IsFvmWithinFtl() const = 0;

  // Checks if the device supports the given partition spec.
  //
  // This is the only function that will definitively say whether a spec is
  // supported or not. Other partition functions may return ZX_ERR_NOT_SUPPORTED
  // on unsupported spec, but they may also return it for other reasons such as
  // a lower-level driver error. They also may return other errors even if given
  // an unsupported spec if something else goes wong.
  virtual bool SupportsPartition(const PartitionSpec& spec) const = 0;

  // Returns a PartitionClient matching |spec|, creating the partition.
  // Assumes that the partition does not already exist.
  virtual zx::status<std::unique_ptr<PartitionClient>> AddPartition(
      const PartitionSpec& spec) const = 0;

  // Returns a PartitionClient matching |spec| if one exists.
  virtual zx::status<std::unique_ptr<PartitionClient>> FindPartition(
      const PartitionSpec& spec) const = 0;

  // Finalizes the PartitionClient matching |spec| after it has been written.
  virtual zx::status<> FinalizePartition(const PartitionSpec& spec) const = 0;

  // Wipes Fuchsia Volume Manager partition.
  virtual zx::status<> WipeFvm() const = 0;

  // Initializes partition tables.
  virtual zx::status<> InitPartitionTables() const = 0;

  // Wipes partition tables.
  virtual zx::status<> WipePartitionTables() const = 0;

  // Determine if the given data file is a valid image for this device.
  //
  // This analysis is best-effort only, providing only basic safety checks.
  virtual zx::status<> ValidatePayload(const PartitionSpec& spec,
                                       fbl::Span<const uint8_t> data) const = 0;

  // Flush all buffered write to persistant storage.
  virtual zx::status<> Flush() const = 0;
};

class DevicePartitionerFactory {
 public:
  // Factory method which automatically returns the correct DevicePartitioner
  // implementation based factories registered with it. Returns nullptr on failure.
  // |block_device| is root block device whichs contains the logical partitions we wish to operate
  // against. It's only meaningful for EFI and CROS devices which may have multiple storage devices.
  static std::unique_ptr<DevicePartitioner> Create(fbl::unique_fd devfs_root,
                                                   const zx::channel& svc_root, Arch arch,
                                                   std::shared_ptr<Context> context,
                                                   zx::channel block_device = zx::channel());

  static void Register(std::unique_ptr<DevicePartitionerFactory> factory);

  virtual ~DevicePartitionerFactory() = default;

 private:
  // This method is overridden by derived classes that implement different kinds
  // of DevicePartitioners.
  virtual zx::status<std::unique_ptr<DevicePartitioner>> New(
      fbl::unique_fd devfs_root, const zx::channel& svc_root, Arch arch,
      std::shared_ptr<Context> context, const fbl::unique_fd& block_device) = 0;

  static std::vector<std::unique_ptr<DevicePartitionerFactory>>* registered_factory_list();
};

// DevicePartitioner implementation for devices which have fixed partition maps (e.g. ARM
// devices). It will not attempt to write a partition map of any kind to the device.
// Assumes legacy partition layout structure (e.g. ZIRCON-A, ZIRCON-B,
// ZIRCON-R).
class FixedDevicePartitioner : public DevicePartitioner {
 public:
  static zx::status<std::unique_ptr<DevicePartitioner>> Initialize(fbl::unique_fd devfs_root);

  bool IsFvmWithinFtl() const override { return false; }

  bool SupportsPartition(const PartitionSpec& spec) const override;

  zx::status<std::unique_ptr<PartitionClient>> AddPartition(
      const PartitionSpec& spec) const override;

  zx::status<std::unique_ptr<PartitionClient>> FindPartition(
      const PartitionSpec& spec) const override;

  zx::status<> FinalizePartition(const PartitionSpec& spec) const override { return zx::ok(); }

  zx::status<> WipeFvm() const override;

  zx::status<> InitPartitionTables() const override;

  zx::status<> WipePartitionTables() const override;

  zx::status<> ValidatePayload(const PartitionSpec& spec,
                               fbl::Span<const uint8_t> data) const override;

  zx::status<> Flush() const override { return zx::ok(); }

 private:
  FixedDevicePartitioner(fbl::unique_fd devfs_root) : devfs_root_(std::move(devfs_root)) {}

  fbl::unique_fd devfs_root_;
};

class DefaultPartitionerFactory : public DevicePartitionerFactory {
 public:
  zx::status<std::unique_ptr<DevicePartitioner>> New(fbl::unique_fd devfs_root,
                                                     const zx::channel& svc_root, Arch arch,
                                                     std::shared_ptr<Context> context,
                                                     const fbl::unique_fd& block_device) final;
};

}  // namespace paver

#endif  // SRC_STORAGE_LIB_PAVER_DEVICE_PARTITIONER_H_
