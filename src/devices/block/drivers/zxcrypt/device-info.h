// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_ZXCRYPT_DEVICE_INFO_H_
#define SRC_DEVICES_BLOCK_DRIVERS_ZXCRYPT_DEVICE_INFO_H_

#include <fuchsia/hardware/block/cpp/banjo.h>
#include <fuchsia/hardware/block/partition/cpp/banjo.h>
#include <fuchsia/hardware/block/volume/cpp/banjo.h>
#include <lib/ddk/device.h>
#include <lib/zx/vmo.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/types.h>

#include <fbl/macros.h>

#include "src/security/zxcrypt/ddk-volume.h"

namespace zxcrypt {

// |zxcrypt::DeviceInfo| bundles block device configuration details passed from the controller to
// the device.  It is used a const struct in |zxcrypt::Device| to allow rapid, lock-free access.
struct DeviceInfo {
  // Callbacks to the parent's block protocol methods.
  ddk::BlockProtocolClient block_protocol;
  // Optional Protocols supported by zxcrypt.
  ddk::BlockPartitionProtocolClient partition_protocol;
  ddk::BlockVolumeProtocolClient volume_protocol;
  // The parent block device
  zx_device_t* block_device;
  // The parent device's block information
  uint32_t block_size;
  // The parent device's required block_op_t size.
  size_t op_size;
  // The number of blocks reserved for metadata.
  uint64_t reserved_blocks;
  // The number of slices reserved for metadata.
  uint64_t reserved_slices;
  // A memory region used for processing I/O transactions.
  zx::vmo vmo;
  // Base address of the VMAR backing the VMO.
  uint8_t* base;

  DeviceInfo(zx_device_t* device, const DdkVolume& volume);
  DeviceInfo(DeviceInfo&& other);
  ~DeviceInfo();
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(DeviceInfo);

  // Returns true if the block device can be used by zxcrypt.  This may fail, for example, if
  // the constructor was unable to get a valid block protocol.
  bool IsValid() const;

  // Reserves a memory region to be used for encrypting and decrypting I/O transactions.  The
  // region will be backed by |vmo| and mapped to |base|.  It will be automatically unmapped when
  // upon this object's destruction.
  zx_status_t Reserve(size_t size);
};

}  // namespace zxcrypt

#endif  // SRC_DEVICES_BLOCK_DRIVERS_ZXCRYPT_DEVICE_INFO_H_
