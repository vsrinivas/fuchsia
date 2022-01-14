// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/block_client/cpp/remote_block_device.h"

#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/device/vfs.h>

namespace fio = fuchsia_io;

namespace block_client {
namespace {

zx_status_t BlockGetFifo(const zx::channel& device, zx::fifo* out_fifo) {
  zx_status_t status, io_status;
  io_status =
      fuchsia_hardware_block_BlockGetFifo(device.get(), &status, out_fifo->reset_and_get_address());
  if (io_status != ZX_OK) {
    return io_status;
  }
  return status;
}

zx_status_t BlockCloseFifo(const zx::channel& device) {
  zx_status_t status, io_status;
  io_status = fuchsia_hardware_block_BlockCloseFifo(device.get(), &status);
  if (io_status != ZX_OK) {
    return io_status;
  }
  return status;
}

}  // namespace

zx_status_t RemoteBlockDevice::FifoTransaction(block_fifo_request_t* requests, size_t count) {
  return fifo_client_.Transaction(requests, count);
}

zx::status<std::string> RemoteBlockDevice::GetDevicePath() const {
  auto resp = fidl::WireCall<fuchsia_device::Controller>(zx::unowned_channel(device_.get()))
                  ->GetTopologicalPath();
  if (auto fidl_error = resp.status(); fidl_error != ZX_OK)
    return zx::error(fidl_error);
  if (resp->result.is_err())
    return zx::error(resp->result.err());
  return zx::ok(std::string(resp->result.response().path.get()));
}

zx_status_t RemoteBlockDevice::BlockGetInfo(fuchsia_hardware_block_BlockInfo* out_info) const {
  zx_status_t status;
  zx_status_t io_status = fuchsia_hardware_block_BlockGetInfo(device_.get(), &status, out_info);
  if (io_status != ZX_OK) {
    return io_status;
  }
  return status;
}

zx_status_t RemoteBlockDevice::BlockAttachVmo(const zx::vmo& vmo, storage::Vmoid* out_vmoid) {
  zx::vmo xfer_vmo;
  zx_status_t status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &xfer_vmo);
  if (status != ZX_OK) {
    return status;
  }

  fuchsia_hardware_block_VmoId vmoid;
  zx_status_t io_status =
      fuchsia_hardware_block_BlockAttachVmo(device_.get(), xfer_vmo.release(), &status, &vmoid);
  if (io_status != ZX_OK) {
    return io_status;
  }
  *out_vmoid = storage::Vmoid(vmoid.id);
  return status;
}

zx_status_t RemoteBlockDevice::VolumeGetInfo(
    fuchsia_hardware_block_volume_VolumeManagerInfo* out_manager_info,
    fuchsia_hardware_block_volume_VolumeInfo* out_volume_info) const {
  // Querying may be used to confirm if the underlying connection is capable of
  // communicating the FVM protocol. Clone the connection, since if the block
  // device does NOT speak the Volume protocol, the connection is terminated.
  zx::channel connection, server;
  zx_status_t status = zx::channel::create(0, &connection, &server);
  if (status != ZX_OK) {
    return status;
  }
  uint32_t flags = ZX_FS_FLAG_CLONE_SAME_RIGHTS;
  auto result = fidl::WireCall<fio::Node>(device_.borrow())->Clone(flags, std::move(server));
  if (result.status() != ZX_OK) {
    return result.status();
  }

  zx_status_t io_status = fuchsia_hardware_block_volume_VolumeGetVolumeInfo(
      connection.get(), &status, out_manager_info, out_volume_info);
  if (io_status != ZX_OK) {
    return io_status;
  }
  return status;
}

zx_status_t RemoteBlockDevice::VolumeQuerySlices(
    const uint64_t* slices, size_t slices_count,
    fuchsia_hardware_block_volume_VsliceRange* out_ranges, size_t* out_ranges_count) const {
  zx_status_t status, io_status;
  io_status = fuchsia_hardware_block_volume_VolumeQuerySlices(
      device_.get(), slices, slices_count, &status, out_ranges, out_ranges_count);
  if (io_status != ZX_OK) {
    return io_status;
  }
  return status;
}

zx_status_t RemoteBlockDevice::VolumeExtend(uint64_t offset, uint64_t length) {
  zx_status_t status, io_status;
  io_status = fuchsia_hardware_block_volume_VolumeExtend(device_.get(), offset, length, &status);
  if (io_status != ZX_OK) {
    return io_status;
  }
  return status;
}

zx_status_t RemoteBlockDevice::VolumeShrink(uint64_t offset, uint64_t length) {
  zx_status_t status, io_status;
  io_status = fuchsia_hardware_block_volume_VolumeShrink(device_.get(), offset, length, &status);
  if (io_status != ZX_OK) {
    return io_status;
  }
  return status;
}

zx_status_t RemoteBlockDevice::Create(zx::channel device, std::unique_ptr<RemoteBlockDevice>* out) {
  zx::fifo fifo;
  zx_status_t status = BlockGetFifo(device, &fifo);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not acquire block fifo: " << status;
    return status;
  }

  *out =
      std::unique_ptr<RemoteBlockDevice>(new RemoteBlockDevice(std::move(device), std::move(fifo)));
  return ZX_OK;
}

RemoteBlockDevice::RemoteBlockDevice(zx::channel device, zx::fifo fifo)
    : device_(std::move(device)), fifo_client_(std::move(fifo)) {}

RemoteBlockDevice::~RemoteBlockDevice() { BlockCloseFifo(device_); }

}  // namespace block_client
