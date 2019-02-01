// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/defer.h>
#include <lib/fxl/logging.h>

#include "mock_netstack.h"

void MockNetstack::AddEthernetDevice(
    std::string topological_path,
    fuchsia::netstack::InterfaceConfig interfaceConfig,
    fidl::InterfaceHandle<::fuchsia::hardware::ethernet::Device> device,
    AddEthernetDeviceCallback callback) {
  auto deferred =
      fit::defer([callback = std::move(callback)]() { callback(0); });
  eth_device_ = device.BindSync();

  zx_status_t status;
  std::unique_ptr<fuchsia::hardware::ethernet::Fifos> fifos;
  eth_device_->GetFifos(&status, &fifos);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get fifos: " << status;
    return;
  }
  rx_ = std::move(fifos->rx);
  tx_ = std::move(fifos->tx);

  status = zx::vmo::create(kVmoSize, ZX_VMO_NON_RESIZABLE, &vmo_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create vmo: " << status;
    return;
  }

  zx::vmo vmo_dup;
  status =
      vmo_.duplicate(ZX_RIGHTS_IO | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER, &vmo_dup);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to duplicate vmo: " << status;
    return;
  }

  eth_device_->SetIOBuffer(std::move(vmo_dup), &status);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to set IO buffer: " << status;
    return;
  }

  status = zx::vmar::root_self()->map(
      0, vmo_, 0, kVmoSize,
      ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_REQUIRE_NON_RESIZABLE,
      &io_addr_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to map vmo: " << status;
    return;
  }

  fuchsia::hardware::ethernet::FifoEntry entry;
  entry.offset = 0;
  entry.length = 100;
  entry.flags = 0;
  entry.cookie = 0;
  status = rx_.write(sizeof(fuchsia::hardware::ethernet::FifoEntry), &entry, 1,
                     nullptr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to write to rx fifo: " << status;
    return;
  }

  eth_device_->Start(&status);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to start ethernet device: " << status;
    return;
  }
}

zx_status_t MockNetstack::SendPacket(void* packet, size_t length) {
  fuchsia::hardware::ethernet::FifoEntry entry;
  entry.offset = 512;
  entry.length = length;
  entry.flags = 0;
  entry.cookie = 0;
  size_t count;
  memcpy(reinterpret_cast<void*>(io_addr_ + entry.offset), packet, length);
  zx_status_t status = tx_.write(sizeof(fuchsia::hardware::ethernet::FifoEntry),
                                 &entry, 1, &count);
  if (status != ZX_OK) {
    return status;
  }
  if (count != 1) {
    return ZX_ERR_INTERNAL;
  }

  zx_signals_t pending = 0;
  status = tx_.wait_one(ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED,
                        zx::deadline_after(kTestTimeout), &pending);
  if (status != ZX_OK) {
    return status;
  } else if (pending & ZX_SOCKET_PEER_CLOSED) {
    return ZX_ERR_PEER_CLOSED;
  }

  status = tx_.read(sizeof(fuchsia::hardware::ethernet::FifoEntry), &entry, 1,
                    nullptr);
  if (status != ZX_OK) {
    return status;
  }
  if (entry.flags != fuchsia::hardware::ethernet::FIFO_TX_OK) {
    return ZX_ERR_IO;
  }

  return ZX_OK;
}

zx_status_t MockNetstack::ReceivePacket(void* packet, size_t length) {
  fuchsia::hardware::ethernet::FifoEntry entry;

  zx_signals_t pending = 0;
  zx_status_t status = rx_.wait_one(ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED,
                                    zx::deadline_after(kTestTimeout), &pending);
  if (status != ZX_OK) {
    return status;
  } else if (pending & ZX_SOCKET_PEER_CLOSED) {
    return ZX_ERR_PEER_CLOSED;
  }

  status = rx_.read(sizeof(fuchsia::hardware::ethernet::FifoEntry), &entry, 1,
                    nullptr);
  if (status != ZX_OK) {
    return status;
  }
  if (entry.flags != fuchsia::hardware::ethernet::FIFO_RX_OK) {
    return ZX_ERR_IO;
  }
  if (entry.length > length) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  memcpy(packet, reinterpret_cast<void*>(io_addr_ + entry.offset), length);

  return ZX_OK;
}