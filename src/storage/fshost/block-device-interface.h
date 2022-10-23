// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_BLOCK_DEVICE_INTERFACE_H_
#define SRC_STORAGE_FSHOST_BLOCK_DEVICE_INTERFACE_H_

#include <fidl/fuchsia.hardware.block.partition/cpp/wire.h>
#include <lib/zx/channel.h>
#include <lib/zx/result.h>
#include <zircon/types.h>

#include <memory>
#include <string_view>

#include "src/lib/storage/fs_management/cpp/mount.h"
#include "src/storage/fshost/copier.h"

namespace fshost {

// An abstract class representing the operations which may be performed
// on a block device, from the perspective of fshost.
class BlockDeviceInterface {
 public:
  virtual ~BlockDeviceInterface() = default;

  zx_status_t Add(bool format_on_corruption = true);

  // Opens a block device at the given topological path.
  // This is effectively a factory method; it is an instance method for overridability but it
  // doesn't interact with the instance.
  virtual zx::result<std::unique_ptr<BlockDeviceInterface>> OpenBlockDevice(
      const char* topological_path) const = 0;
  // Opens a block device given a file descriptor.
  virtual zx::result<std::unique_ptr<BlockDeviceInterface>> OpenBlockDeviceByFd(
      fbl::unique_fd fd) const = 0;

  // When the filesystem inside the device is mounted, this data will be inserted into the
  // filesystem.  If called repeatedly, only the most recent data is inserted.
  virtual void AddData(Copier) = 0;

  // Attempt to extract the data out of the block device (which should be formatted as a mutable
  // filesystem, e.g. minfs).
  virtual zx::result<Copier> ExtractData() = 0;

  // Returns the format that the content appears to be.  Avoid using this unless
  // there is no other way to determine the format of the device.
  virtual fs_management::DiskFormat content_format() const = 0;

  // The topological path for the device.
  virtual const std::string& topological_path() const = 0;

  // The partition name for this device (if it happens to be part of a partiiton scheme).
  virtual const std::string& partition_name() const = 0;

  // Returns the expected on-disk format of the underlying device.
  //
  // If unknown or unreadable, fs_management::kDiskFormatUnknown should be returned.
  virtual fs_management::DiskFormat GetFormat() = 0;

  // Modifies the expected on-disk format of the underlying device.
  //
  // This may be useful if the block device data was corrupted, and we want
  // to force a new format based on external information.
  virtual void SetFormat(fs_management::DiskFormat format) = 0;

  // Queries (using the block interface) for info about the underlying device.
  virtual zx::result<fuchsia_hardware_block::wire::BlockInfo> GetInfo() const = 0;

  // Queries (using the partition interface) for the instance/type GUID of the underlying device.
  // Returns a GUID with all 0 bytes on failure, normally this means the device doesn't support the
  // Partition interface.
  virtual const fuchsia_hardware_block_partition::wire::Guid& GetInstanceGuid() const = 0;
  virtual const fuchsia_hardware_block_partition::wire::Guid& GetTypeGuid() const = 0;

  // Attempts to directly bind a driver to the device. This is typically used
  // by partition drivers, which may be loaded on top of a device exposing the
  // block interface.
  virtual zx_status_t AttachDriver(const std::string_view& driver) = 0;

  // Unseals the underlying zxcrypt volume.
  virtual zx_status_t UnsealZxcrypt() = 0;

  // Creates the zxcrypt partition.
  virtual zx_status_t FormatZxcrypt() = 0;

  // Returns true if the consistency of filesystems should be validated before
  // mounting.
  virtual bool ShouldCheckFilesystems() = 0;

  // Validates the state of the filesystem, and returns ZX_OK if it appears
  // consistent (or if the consistency check should be skipped).
  virtual zx_status_t CheckFilesystem() = 0;

  // Reformats the underlying block device with the format returned by |GetFormat()|.
  virtual zx_status_t FormatFilesystem() = 0;

  // Attempts to mount the filesystem with the format returned by |GetFormat()|.
  virtual zx_status_t MountFilesystem() = 0;

  // Queries the seal used to open the verity device.
  virtual zx::result<std::string> VeritySeal() = 0;

  // Opens the block-verity device for reading.
  virtual zx_status_t OpenBlockVerityForVerifiedRead(std::string seal_hex) = 0;

  // Queries if we should allow factory partition modifications.
  virtual bool ShouldAllowAuthoringFactory() = 0;

  // Sets the maximum size in FVM (at the given device path) for this device.
  virtual zx_status_t SetPartitionMaxSize(const std::string& fvm_path, uint64_t max_size) = 0;

  // Queries if the device is a block device or a NAND device.
  virtual bool IsNand() const = 0;

  // Queries if the device is a ram-disk.
  virtual bool IsRamDisk() const = 0;

  // Sets the partitio name in FVM (at the given device path) for this device.
  virtual zx_status_t SetPartitionName(const std::string& fvm_path, std::string_view name) = 0;
};

}  // namespace fshost

#endif  // SRC_STORAGE_FSHOST_BLOCK_DEVICE_INTERFACE_H_
