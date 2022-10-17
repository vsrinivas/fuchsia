// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/block_client/cpp/remote_block_device.h"

#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.block/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/vmo.h>

#include "src/lib/storage/block_client/cpp/reader.h"
#include "src/lib/storage/block_client/cpp/writer.h"

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

zx::result<std::string> RemoteBlockDevice::GetDevicePath() const {
  auto resp = fidl::WireCall<fuchsia_device::Controller>(zx::unowned_channel(device_.get()))
                  ->GetTopologicalPath();
  if (auto fidl_error = resp.status(); fidl_error != ZX_OK)
    return zx::error(fidl_error);
  if (resp->is_error())
    return zx::error(resp->error_value());
  return zx::ok(std::string(resp->value()->path.get()));
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
  fio::wire::OpenFlags flags = fio::wire::OpenFlags::kCloneSameRights;
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

zx::result<std::unique_ptr<RemoteBlockDevice>> RemoteBlockDevice::Create(int fd) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Node>();
  if (!endpoints.is_ok()) {
    return endpoints.take_error();
  }

  fdio_cpp::UnownedFdioCaller caller(fd);
  auto status = fidl::WireCall(caller.node())
                    ->Clone(fio::wire::OpenFlags::kCloneSameRights, std::move(endpoints->server))
                    .status();
  if (status != ZX_OK) {
    return zx::error(status);
  }
  std::unique_ptr<block_client::RemoteBlockDevice> client;
  status = block_client::RemoteBlockDevice::Create(endpoints->client.TakeChannel(), &client);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(std::move(client));
}

RemoteBlockDevice::RemoteBlockDevice(zx::channel device, zx::fifo fifo)
    : device_(std::move(device)), fifo_client_(std::move(fifo)) {}

RemoteBlockDevice::~RemoteBlockDevice() { BlockCloseFifo(device_); }

zx_status_t ReadWriteBlocks(int fd, void* buffer, size_t buffer_length, size_t offset, bool write) {
  zx_status_t status;
  fdio_cpp::UnownedFdioCaller caller(fd);
  zx_handle_t device = caller.borrow_channel();

  // Get the Block info for block size calculations:
  fuchsia_hardware_block_BlockInfo info;
  zx_status_t io_status = fuchsia_hardware_block_BlockGetInfo(device, &status, &info);
  if (io_status != ZX_OK) {
    return io_status;
  }
  if (status != ZX_OK) {
    return status;
  }

  zx::vmo vmo;
  status = zx::vmo::create(buffer_length, 0, &vmo);
  if (status != ZX_OK) {
    return status;
  }

  size_t block_size = info.block_size;

  if (!buffer || buffer_length % block_size != 0 || offset % block_size != 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx::vmo read_vmo;
  zx_status_t rw_status;
  if (write) {
    vmo.write(buffer, 0, buffer_length);
    status = fuchsia_hardware_block_BlockWriteBlocks(device, vmo.release(), buffer_length, offset,
                                                     0, &rw_status);
  } else {
    // if reading, duplicate the vmo so we will retain a copy
    status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &read_vmo);
    if (status != ZX_OK) {
      return status;
    }
    status = fuchsia_hardware_block_BlockReadBlocks(device, vmo.release(), buffer_length, offset, 0,
                                                    &rw_status);
  }

  if (status != ZX_OK) {
    return status;
  }
  if (rw_status != ZX_OK) {
    return rw_status;
  }

  if (!write) {
    return read_vmo.read(buffer, 0, buffer_length);
  }
  return ZX_OK;
}

zx_status_t SingleReadBytes(int fd, void* buffer, size_t buffer_size, size_t offset) {
  return ReadWriteBlocks(fd, buffer, buffer_size, offset, false);
}

zx_status_t SingleWriteBytes(int fd, void* buffer, size_t buffer_size, size_t offset) {
  return ReadWriteBlocks(fd, buffer, buffer_size, offset, true);
}
}  // namespace block_client
