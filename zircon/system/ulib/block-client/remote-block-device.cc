// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/io/c/fidl.h>
#include <zircon/device/vfs.h>

#include <block-client/cpp/remote-block-device.h>
#include <fs/trace.h>

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

zx_status_t RemoteBlockDevice::ReadBlock(uint64_t block_num, uint64_t block_size,
                                         void* block) const {
  uint64_t offset = block_num * block_size;
  size_t actual;
  zx_status_t status, io_status;
  io_status = fuchsia_io_FileReadAt(device_.get(), block_size, offset, &status,
                                    reinterpret_cast<uint8_t*>(block), block_size, &actual);
  if (io_status != ZX_OK) {
    return io_status;
  }
  if (status != ZX_OK) {
    return status;
  }
  if (actual != block_size) {
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

zx_status_t RemoteBlockDevice::FifoTransaction(block_fifo_request_t* requests, size_t count) {
  return fifo_client_.Transaction(requests, count);
}

zx_status_t RemoteBlockDevice::GetDevicePath(size_t buffer_len, char* out_name,
                                             size_t* out_len) const {
  if (buffer_len == 0) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  zx_status_t status, io_status;

  auto resp = ::llcpp::fuchsia::device::Controller::Call::GetTopologicalPath(
      zx::unowned_channel(device_.get()));

  io_status = resp.status();
  if (io_status != ZX_OK) {
    return io_status;
  }

  if (resp->result.is_err()) {
    status = resp->result.err();
  } else {
    auto r = resp->result.response();
    *out_len = r.path.size();
    memcpy(out_name, r.path.data(), r.path.size());
    status = ZX_OK;
  }

  if (status != ZX_OK) {
    return status;
  }
  // Ensure null-terminated
  out_name[*out_len] = 0;
  // Account for the null byte in the length, since callers expect it.
  (*out_len)++;
  return ZX_OK;
}

zx_status_t RemoteBlockDevice::BlockGetInfo(fuchsia_hardware_block_BlockInfo* out_info) const {
  zx_status_t status;
  zx_status_t io_status = fuchsia_hardware_block_BlockGetInfo(device_.get(), &status, out_info);
  if (io_status != ZX_OK) {
    return io_status;
  }
  return status;
}

zx_status_t RemoteBlockDevice::BlockAttachVmo(const zx::vmo& vmo,
                                              fuchsia_hardware_block_VmoId* out_vmoid) {
  zx::vmo xfer_vmo;
  zx_status_t status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &xfer_vmo);
  if (status != ZX_OK) {
    return status;
  }

  zx_status_t io_status =
      fuchsia_hardware_block_BlockAttachVmo(device_.get(), xfer_vmo.release(), &status, out_vmoid);
  if (io_status != ZX_OK) {
    return io_status;
  }
  return status;
}

zx_status_t RemoteBlockDevice::VolumeQuery(
    fuchsia_hardware_block_volume_VolumeInfo* out_info) const {
  // Querying may be used to confirm if the underlying connection is capable of
  // communicating the FVM protocol. Clone the connection, since if the block
  // device does NOT speak the Volume protocol, the connection is terminated.
  zx::channel connection, server;
  zx_status_t status = zx::channel::create(0, &connection, &server);
  if (status != ZX_OK) {
    return status;
  }
  uint32_t flags = ZX_FS_FLAG_CLONE_SAME_RIGHTS;
  status = fuchsia_io_NodeClone(device_.get(), flags, server.release());
  if (status != ZX_OK) {
    return status;
  }

  zx_status_t io_status =
      fuchsia_hardware_block_volume_VolumeQuery(connection.get(), &status, out_info);
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
    FS_TRACE_ERROR("Could not acquire block fifo: %d\n", status);
    return status;
  }
  block_client::Client fifo_client;
  status = block_client::Client::Create(std::move(fifo), &fifo_client);
  if (status != ZX_OK) {
    return status;
  }
  *out = std::unique_ptr<RemoteBlockDevice>(
      new RemoteBlockDevice(std::move(device), std::move(fifo_client)));
  return ZX_OK;
}

RemoteBlockDevice::RemoteBlockDevice(zx::channel device, block_client::Client fifo_client)
    : device_(std::move(device)), fifo_client_(std::move(fifo_client)) {}

RemoteBlockDevice::~RemoteBlockDevice() { BlockCloseFifo(device_); }

}  // namespace block_client
