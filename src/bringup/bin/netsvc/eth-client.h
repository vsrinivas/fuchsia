// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_NETSVC_ETH_CLIENT_H_
#define SRC_BRINGUP_BIN_NETSVC_ETH_CLIENT_H_

#include <fidl/fuchsia.hardware.ethernet/cpp/wire.h>
#include <lib/zx/fifo.h>
#include <lib/zx/status.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

class EthClient {
 public:
  static zx::status<std::unique_ptr<EthClient>> Create(
      fidl::ClientEnd<fuchsia_hardware_ethernet::Device> client_end, zx::vmo io_vmo, void* io_mem);

  // Enqueue a packet for transmit.
  zx_status_t QueueTx(void* cookie, void* data, size_t len);
  // Process all transmitted buffers.
  zx_status_t CompleteTx(void* ctx, void (*func)(void* ctx, void* cookie));
  // Enqueue a packet for reception.
  zx_status_t QueueRx(void* cookie, void* data, size_t len);
  // Process all received buffers.
  zx_status_t CompleteRx(void* ctx, void (*func)(void* ctx, void* cookie, size_t len));
  // Wait for completed rx packets.
  //
  // Returns true if the device is signaling a new status is available to be
  // read through `GetStatus`.
  //
  // ZX_ERR_PEER_CLOSED - far side disconnected.
  // ZX_ERR_TIMED_OUT - deadline lapsed.
  zx::status<bool> WaitRx(zx::time deadline);

  // Wait for completed tx packets.
  //
  // ZX_ERR_PEER_CLOSED - far side disconnected.
  // ZX_ERR_TIMED_OUT - deadline lapsed.
  // ZX_OK - completed packets are available.
  zx_status_t WaitTx(zx::time deadline);

  zx::status<fuchsia_hardware_ethernet::wire::DeviceStatus> GetStatus();

 private:
  EthClient(fidl::WireSyncClient<fuchsia_hardware_ethernet::Device> device, zx::fifo tx,
            uint32_t tx_size, zx::fifo rx, uint32_t rx_size, void* io_mem)
      : device_(std::move(device)),
        tx_fifo_(std::move(tx)),
        rx_fifo_(std::move(rx)),
        tx_size_(tx_size),
        rx_size_(rx_size),
        io_mem_(reinterpret_cast<uintptr_t>(io_mem)) {}
  fidl::WireSyncClient<fuchsia_hardware_ethernet::Device> device_;
  zx::fifo tx_fifo_;
  zx::fifo rx_fifo_;
  const uint32_t tx_size_;
  const uint32_t rx_size_;
  const uintptr_t io_mem_;
};

#endif  // SRC_BRINGUP_BIN_NETSVC_ETH_CLIENT_H_
