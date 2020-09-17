// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_BLOCK_DEVICE_INTERFACE_H_
#define SRC_STORAGE_FSHOST_BLOCK_DEVICE_INTERFACE_H_

#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/hardware/block/partition/c/fidl.h>
#include <lib/zx/channel.h>
#include <lib/zx/status.h>
#include <zircon/types.h>

#include <memory>
#include <string_view>

#include <fbl/string_piece.h>
#include <fs-management/mount.h>

namespace devmgr {

constexpr char kFVMDriverPath[] = "/boot/driver/fvm.so";
constexpr char kGPTDriverPath[] = "/boot/driver/gpt.so";
constexpr char kMBRDriverPath[] = "/boot/driver/mbr.so";
constexpr char kZxcryptDriverPath[] = "/boot/driver/zxcrypt.so";
constexpr char kBootpartDriverPath[] = "/boot/driver/bootpart.so";
constexpr char kBlockVerityDriverPath[] = "/boot/driver/block-verity.so";

// An abstract class representing the operations which may be performed
// on a block device, from the perspective of fshost.
class BlockDeviceInterface {
 public:
  virtual ~BlockDeviceInterface() = default;

  zx_status_t Add();

  // Returns the format that the content appears to be.  Avoid using this unless
  // there is no other way to determine the format of the device.
  virtual disk_format_t content_format() const = 0;

  // The topological path for the device.
  virtual const std::string& topological_path() const = 0;

  // The partition name for this device (if it happens to be part of a partiiton scheme).
  virtual const std::string& partition_name() const = 0;

  // Returns the expected on-disk format of the underlying device.
  //
  // If unknown or unreadable, DISK_FORMAT_UNKNOWN should be returned.
  virtual disk_format_t GetFormat() = 0;

  // Modifies the expected on-disk format of the underlying device.
  //
  // This may be useful if the block device data was corrupted, and we want
  // to force a new format based on external information.
  virtual void SetFormat(disk_format_t format) = 0;

  // Queries (using the block interface) for info about the underlying device.
  virtual zx_status_t GetInfo(fuchsia_hardware_block_BlockInfo* out_info) const = 0;

  // Queries (using the partition interface) for the GUID of the underlying device.
  virtual const fuchsia_hardware_block_partition_GUID& GetTypeGuid() const = 0;

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
  virtual zx::status<std::string> VeritySeal() = 0;

  // Opens the block-verity device for reading.
  virtual zx_status_t OpenBlockVerityForVerifiedRead(std::string seal_hex) = 0;

  // Queries if we should allow factory partition modifications.
  virtual bool ShouldAllowAuthoringFactory() = 0;
};

}  // namespace devmgr

#endif  // SRC_STORAGE_FSHOST_BLOCK_DEVICE_INTERFACE_H_
