// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_NETSVC_ETH_CLIENT_H_
#define SRC_BRINGUP_BIN_NETSVC_ETH_CLIENT_H_

#include <fidl/fuchsia.hardware.ethernet/cpp/wire.h>
#include <lib/async/cpp/wait.h>
#include <lib/zx/fifo.h>
#include <lib/zx/status.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

class EthClient {
 public:
  static zx::result<std::unique_ptr<EthClient>> Create(
      async_dispatcher_t* dispatcher, fidl::ClientEnd<fuchsia_hardware_ethernet::Device> client_end,
      zx::vmo io_vmo, void* io_mem, fit::closure on_rx, fit::closure on_status,
      fit::closure on_closed);

  // Enqueue a packet for transmit.
  zx_status_t QueueTx(void* cookie, void* data, size_t len);
  // Process all transmitted buffers.
  zx_status_t CompleteTx(void* ctx, void (*func)(void* ctx, void* cookie));
  // Enqueue a packet for reception.
  zx_status_t QueueRx(void* cookie, void* data, size_t len);
  // Process all received buffers.
  zx_status_t CompleteRx(void* ctx, void (*func)(async_dispatcher_t* dispatcher, void* ctx,
                                                 void* cookie, size_t len));

  // Waits for rx signals, calling |on_rx| when rx frames are available and
  // |on_status| when the device signals its online status has changed.
  zx_status_t WatchRx(async_dispatcher_t* dispatcher, fit::closure on_rx, fit::closure on_status);

  // Wait for completed tx packets.
  //
  // ZX_ERR_PEER_CLOSED - far side disconnected.
  // ZX_ERR_TIMED_OUT - deadline lapsed.
  // ZX_OK - completed packets are available.
  zx_status_t WaitTx(zx::time deadline);

  zx::result<fuchsia_hardware_ethernet::wire::DeviceStatus> GetStatus();

 private:
  void OnRxSignal(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                  const zx_packet_signal_t* signal);

  EthClient(async_dispatcher_t* dispatcher,
            fidl::WireSyncClient<fuchsia_hardware_ethernet::Device> device, zx::fifo tx,
            uint32_t tx_size, zx::fifo rx, uint32_t rx_size, void* io_mem, fit::closure on_rx,
            fit::closure on_signal, fit::callback<void()> on_closed)
      : device_(std::move(device)),
        tx_fifo_(std::move(tx)),
        rx_fifo_(std::move(rx)),
        dispatcher_(dispatcher),
        on_rx_(std::move(on_rx)),
        on_signal_(std::move(on_signal)),
        on_closed_(std::move(on_closed)),
        rx_task_(this, rx_fifo_.get(),
                 ZX_FIFO_PEER_CLOSED | ZX_FIFO_READABLE |
                     fuchsia_hardware_ethernet::wire::kSignalStatus),
        tx_size_(tx_size),
        rx_size_(rx_size),
        io_mem_(reinterpret_cast<uintptr_t>(io_mem)) {
    zx_status_t status = rx_task_.Begin(dispatcher);
    ZX_ASSERT_MSG(status == ZX_OK, "failed to schedule task %s", zx_status_get_string(status));
  }
  fidl::WireSyncClient<fuchsia_hardware_ethernet::Device> device_;
  zx::fifo tx_fifo_;
  zx::fifo rx_fifo_;
  async_dispatcher_t* const dispatcher_;
  const fit::closure on_rx_;
  const fit::closure on_signal_;
  fit::callback<void()> on_closed_;
  async::WaitMethod<EthClient, &EthClient::OnRxSignal> rx_task_;
  const uint32_t tx_size_;
  const uint32_t rx_size_;
  const uintptr_t io_mem_;
};

#endif  // SRC_BRINGUP_BIN_NETSVC_ETH_CLIENT_H_
