// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include <fs-management/mount.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/hardware/block/partition/c/fidl.h>
#include <lib/zx/channel.h>
#include <zircon/types.h>

namespace devmgr {

constexpr char kFVMDriverPath[] = "/boot/driver/fvm.so";
constexpr char kGPTDriverPath[] = "/boot/driver/gpt.so";
constexpr char kMBRDriverPath[] = "/boot/driver/mbr.so";
constexpr char kZxcryptDriverPath[] = "/boot/driver/zxcrypt.so";
constexpr char kBootpartDriverPath[] = "/boot/driver/bootpart.so";

// An abstract class representing the operations which may be performed
// on a block device, from the perspective of fshost.
class BlockDeviceInterface {
public:
    virtual ~BlockDeviceInterface() = default;

    zx_status_t Add();

private:
    // Returns the expected on-disk format of the underlying device.
    //
    // If unknown or unreadable, DISK_FORMAT_UNKNOWN should be returned.
    virtual disk_format_t GetFormat() = 0;

    // Modifies the expected on-disk format of the underlying device.
    //
    // This may be useful if the block device data was corrupted, and we want
    // to force a new format based on external information.
    virtual void SetFormat(disk_format_t format) = 0;

    // Returns "true" if the device is booted from in-memory partitions,
    // and expects that filesystems and encrypted partitions will not be
    // automatically mounted.
    virtual bool Netbooting() = 0;

    // Queries (using the block interface) for info about the underlying device.
    virtual zx_status_t GetInfo(fuchsia_hardware_block_BlockInfo* out_info) = 0;

    // Queries (using the partition interface) for the GUID of the underlying device.
    virtual zx_status_t GetTypeGUID(fuchsia_hardware_block_partition_GUID* out_guid) = 0;

    // Attempts to directly bind a driver to the device. This is typically used
    // by partition drivers, which may be loaded on top of a device exposing the
    // block interface.
    virtual zx_status_t AttachDriver(const fbl::StringPiece& driver) = 0;

    // Unseals the underlying zxcrypt volume.
    virtual zx_status_t UnsealZxcrypt() = 0;

    // Creates the zxcrypt partition.
    virtual zx_status_t FormatZxcrypt() = 0;

    // Determines if the underlying volume is unsealed zxcrypt. Assumes the device
    // has the data GUID.
    virtual zx_status_t IsUnsealedZxcrypt(bool* is_unsealed_zxcrypt) = 0;

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
};

} // namespace devmgr
