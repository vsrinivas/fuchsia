// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/netsvc/eth-client.h"

#include <stdio.h>
#include <zircon/device/ethernet.h>
#include <zircon/syscalls.h>

#include <fbl/alloc_checker.h>

#if 0
#define IORING_TRACE(fmt...) fprintf(stderr, fmt)
#else
#define IORING_TRACE(fmt...)
#endif

zx::status<std::unique_ptr<EthClient>> EthClient::Create(
    fidl::ClientEnd<fuchsia_hardware_ethernet::Device> client_end, zx::unowned_vmo io_vmo,
    void* io_mem) {
  fidl::WireSyncClient eth = fidl::BindSyncClient(std::move(client_end));

  fidl::WireResult r = eth->GetFifos();
  if (!r.ok()) {
    fprintf(stderr, "%s: failed to get fifos: %s\n", __FUNCTION__, r.status_string());
    return zx::error(r.status());
  }
  if (zx_status_t status = r.value().status; status != ZX_OK) {
    fprintf(stderr, "%s: GetFifos error: %s\n", __FUNCTION__, zx_status_get_string(status));
    return zx::error(status);
  }
  fuchsia_hardware_ethernet::wire::Fifos& fifos = *r.value().info;

  zx::vmo device_vmo;
  if (zx_status_t status = io_vmo->duplicate(ZX_RIGHT_SAME_RIGHTS, &device_vmo); status != ZX_OK) {
    fprintf(stderr, "%s: failed to duplicate vmo %s\n", __FUNCTION__, zx_status_get_string(status));
  }

  {
    fidl::WireResult result = eth->SetIoBuffer(std::move(device_vmo));
    if (!result.ok()) {
      fprintf(stderr, "%s: failed to set iobuf: %s\n", __FUNCTION__, result.status_string());
      return zx::error(result.status());
    }
    if (zx_status_t status = result.value().status; status != ZX_OK) {
      fprintf(stderr, "%s: set iobuf error: %s\n", __FUNCTION__, zx_status_get_string(status));
      return zx::error(status);
    }
  }

  {
    fidl::WireResult result = eth->SetClientName("netsvc");
    if (!result.ok()) {
      fprintf(stderr, "%s: failed to set client name %s\n", __FUNCTION__, result.status_string());
      return zx::error(result.status());
    }
    if (zx_status_t status = result.value().status; status != ZX_OK) {
      fprintf(stderr, "%s: set client name error %s\n", __FUNCTION__, zx_status_get_string(status));
      return zx::error(status);
    }
  }

  {
    fidl::WireResult result = eth->Start();
    if (!result.ok()) {
      fprintf(stderr, "%s: failed to start device %s\n", __FUNCTION__, result.status_string());
      return zx::error(result.status());
    }
    if (zx_status_t status = result.value().status; status != ZX_OK) {
      fprintf(stderr, "%s: device error %s\n", __FUNCTION__, result.status_string());
      return zx::error(status);
    }
  }

  fbl::AllocChecker ac;
  std::unique_ptr<EthClient> cli(new (&ac)
                                     EthClient(std::move(eth), std::move(fifos.tx), fifos.tx_depth,
                                               std::move(fifos.rx), fifos.rx_depth, io_mem));
  if (!ac.check()) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }

  return zx::ok(std::move(cli));
}

zx_status_t EthClient::QueueTx(void* cookie, void* data, size_t len) {
  eth_fifo_entry_t e = {
      .offset = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data) - io_mem_),
      .length = static_cast<uint16_t>(len),
      .cookie = reinterpret_cast<uint64_t>(cookie),
  };
  IORING_TRACE("eth:tx+ c=0x%08lx o=%u l=%u f=%u\n", e.cookie, e.offset, e.length, e.flags);
  return tx_fifo_.write(sizeof(e), &e, 1, nullptr);
}

zx_status_t EthClient::QueueRx(void* cookie, void* data, size_t len) {
  eth_fifo_entry_t e = {
      .offset = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data) - io_mem_),
      .length = static_cast<uint16_t>(len),
      .cookie = reinterpret_cast<uint64_t>(cookie),
  };
  IORING_TRACE("eth:rx+ c=0x%08lx o=%u l=%u f=%u\n", e.cookie, e.offset, e.length, e.flags);
  return rx_fifo_.write(sizeof(e), &e, 1, nullptr);
}

namespace {

template <typename F>
zx_status_t CompleteFifo(zx::fifo& fifo, uint32_t depth, F process_entry) {
  eth_fifo_entry_t entries[depth];
  size_t count;
  if (zx_status_t status = fifo.read(sizeof(entries[0]), entries, countof(entries), &count);
      status != ZX_OK) {
    if (status == ZX_ERR_SHOULD_WAIT) {
      return ZX_OK;
    }
    return status;
  }

  for (eth_fifo_entry_t* e = entries; count-- > 0; e++) {
    IORING_TRACE("eth:tx- c=0x%08lx o=%u l=%u f=%u\n", e->cookie, e->offset, e->length, e->flags);
    process_entry(*e);
  }
  return ZX_OK;
}

}  // namespace

zx_status_t EthClient::CompleteTx(void* ctx, void (*func)(void* ctx, void* cookie)) {
  return CompleteFifo(tx_fifo_, tx_size_, [ctx, func](const eth_fifo_entry_t& e) {
    IORING_TRACE("eth:tx- c=0x%08lx o=%u l=%u f=%u\n", e.cookie, e.offset, e.length, e.flags);
    func(ctx, reinterpret_cast<void*>(e.cookie));
  });
}

zx_status_t EthClient::CompleteRx(void* ctx, void (*func)(void* ctx, void* cookie, size_t len)) {
  return CompleteFifo(rx_fifo_, rx_size_, [ctx, func](const eth_fifo_entry_t& e) {
    IORING_TRACE("eth:rx- c=0x%08lx o=%u l=%u f=%u\n", e.cookie, e.offset, e.length, e.flags);
    func(ctx, reinterpret_cast<void*>(e.cookie), e.length);
  });
}

namespace {

zx::status<zx_signals_t> WaitFifo(zx::fifo& fifo, zx::time deadline,
                                  zx_signals_t extra_signals = 0) {
  zx_signals_t signals;
  if (zx_status_t status =
          fifo.wait_one(ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED | extra_signals, deadline, &signals);
      status != ZX_OK) {
    return zx::error(status);
  }
  if (signals & ZX_FIFO_PEER_CLOSED) {
    return zx::error(ZX_ERR_PEER_CLOSED);
  }
  return zx::ok(signals);
}

}  // namespace

zx::status<bool> EthClient::WaitRx(zx::time deadline) {
  zx::status status = WaitFifo(rx_fifo_, deadline, fuchsia_hardware_ethernet::wire::kSignalStatus);
  if (status.is_error()) {
    return status.take_error();
  }
  return zx::ok((status.value() & fuchsia_hardware_ethernet::wire::kSignalStatus) != 0);
}

zx_status_t EthClient::WaitTx(zx::time deadline) {
  return WaitFifo(tx_fifo_, deadline).status_value();
}

zx::status<fuchsia_hardware_ethernet::wire::DeviceStatus> EthClient::GetStatus() {
  fidl::WireResult result = device_->GetStatus();
  if (!result.ok()) {
    return zx::error(result.status());
  }
  return zx::ok(result.value().device_status);
}
