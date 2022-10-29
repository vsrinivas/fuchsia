// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_BLOCK_CLIENT_CPP_BLOCK_DEVICE_H_
#define SRC_LIB_STORAGE_BLOCK_CLIENT_CPP_BLOCK_DEVICE_H_

#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.block.volume/cpp/wire.h>
#include <fidl/fuchsia.hardware.block/cpp/wire.h>
#include <lib/zx/result.h>
#include <lib/zx/vmo.h>

#include <memory>

#include <storage/buffer/vmoid_registry.h>

namespace block_client {

// A high-level interface to a block device. This class also inherits from VmoidRegistry for
// managing the VMOs associated with block requests.
//
// The normal implementation would be a RemoteBlockDevice which speaks the FIDL/FIFO protocols
class BlockDevice : public storage::VmoidRegistry {
 public:
  // FIFO protocol. This is the normal way to read from and write to the block device.
  virtual zx_status_t FifoTransaction(block_fifo_request_t* requests, size_t count) = 0;

  // Queries the device path using the fuchsia.device.Controller interface.
  virtual zx::result<std::string> GetDevicePath() const = 0;

  // fuchsia.device.block interface:
  virtual zx_status_t BlockGetInfo(fuchsia_hardware_block::wire::BlockInfo* out_info) const = 0;

  // storage::VmoidRegistry implementation:
  //
  // Derived classes will need to implement BlockAttachVmo() according to their requirements. This
  // implementation implements detach by sending a FIFO transaction which should work for most
  // cases.
  zx_status_t BlockDetachVmo(storage::Vmoid vmoid) override;

  // fuchsia.hardware.block.volume interface:
  //
  // Many block devices (like normal disk partitions) are volumes. This provides a convenience
  // wrapper for speaking the fuchsia.hardware.block.Volume FIDL API to the device.
  //
  // If the underlying device does not speak the Volume API, the connection used by this object
  // will be closed. The exception is VolumeGetInfo() which is implemented such that the connection
  // will still be usable. Clients should call VolumeGetInfo() to confirm that the device supports
  // the Volume API before using any other Volume methods.
  virtual zx_status_t VolumeGetInfo(
      fuchsia_hardware_block_volume::wire::VolumeManagerInfo* out_manager_info,
      fuchsia_hardware_block_volume::wire::VolumeInfo* out_volume_info) const = 0;
  virtual zx_status_t VolumeQuerySlices(
      const uint64_t* slices, size_t slices_count,
      fuchsia_hardware_block_volume::wire::VsliceRange* out_ranges,
      size_t* out_ranges_count) const = 0;
  virtual zx_status_t VolumeExtend(uint64_t offset, uint64_t length) = 0;
  virtual zx_status_t VolumeShrink(uint64_t offset, uint64_t length) = 0;
};

}  // namespace block_client

#endif  // SRC_LIB_STORAGE_BLOCK_CLIENT_CPP_BLOCK_DEVICE_H_
