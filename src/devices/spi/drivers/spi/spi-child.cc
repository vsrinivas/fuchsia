// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spi-child.h"

#include <ddktl/fidl.h>
#include <fbl/vector.h>

#include "spi.h"

namespace spi {

namespace sharedmemory = ::llcpp::fuchsia::hardware::sharedmemory;

void SpiChild::Transmit(fidl::VectorView<uint8_t> data, TransmitCompleter::Sync& completer) {
  size_t actual;
  spi_.Exchange(cs_, data.data(), data.count(), nullptr, 0, &actual);
  completer.Reply(ZX_OK);
}

void SpiChild::Receive(uint32_t size, ReceiveCompleter::Sync& completer) {
  fbl::Vector<uint8_t> rxdata;
  rxdata.reserve(size);
  size_t actual;
  spi_.Exchange(cs_, nullptr, 0, rxdata.begin(), size, &actual);
  fidl::VectorView<uint8_t> rx_vector(fidl::unowned_ptr(rxdata.data()), size);
  completer.Reply(ZX_OK, std::move(rx_vector));
}

void SpiChild::Exchange(fidl::VectorView<uint8_t> txdata, ExchangeCompleter::Sync& completer) {
  fbl::Vector<uint8_t> rxdata;
  size_t size = txdata.count();
  rxdata.reserve(size);
  size_t actual;
  spi_.Exchange(cs_, txdata.data(), size, rxdata.begin(), size, &actual);
  fidl::VectorView<uint8_t> rx_vector(fidl::unowned_ptr(rxdata.data()), size);
  completer.Reply(ZX_OK, std::move(rx_vector));
}

void SpiChild::TransmitVector(::fidl::VectorView<uint8_t> data,
                              TransmitVectorCompleter::Sync& completer) {
  size_t rx_actual;
  zx_status_t status = spi_.Exchange(cs_, data.data(), data.count(), nullptr, 0, &rx_actual);
  if (status == ZX_OK) {
    completer.Reply(ZX_OK);
  } else {
    completer.Reply(status);
  }
}

void SpiChild::ReceiveVector(uint32_t size, ReceiveVectorCompleter::Sync& completer) {
  fbl::Vector<uint8_t> rxdata;
  rxdata.reserve(size);
  size_t rx_actual;
  zx_status_t status = spi_.Exchange(cs_, nullptr, 0, rxdata.begin(), size, &rx_actual);
  if (status == ZX_OK && rx_actual == size) {
    fidl::VectorView<uint8_t> rx_vector(fidl::unowned_ptr(rxdata.data()), size);
    completer.Reply(ZX_OK, std::move(rx_vector));
  } else {
    completer.Reply(status == ZX_OK ? ZX_ERR_INTERNAL : status, fidl::VectorView<uint8_t>());
  }
}

void SpiChild::ExchangeVector(::fidl::VectorView<uint8_t> txdata,
                              ExchangeVectorCompleter::Sync& completer) {
  fbl::Vector<uint8_t> rxdata;
  const size_t size = txdata.count();
  rxdata.reserve(size);
  size_t rx_actual;
  zx_status_t status = spi_.Exchange(cs_, txdata.data(), size, rxdata.begin(), size, &rx_actual);
  if (status == ZX_OK && rx_actual == size) {
    fidl::VectorView<uint8_t> rx_vector(fidl::unowned_ptr(rxdata.data()), size);
    completer.Reply(ZX_OK, std::move(rx_vector));
  } else {
    completer.Reply(status == ZX_OK ? ZX_ERR_INTERNAL : status, fidl::VectorView<uint8_t>());
  }
}

void SpiChild::RegisterVmo(uint32_t vmo_id, ::zx::vmo vmo, uint64_t offset, uint64_t size,
                           RegisterVmoCompleter::Sync& completer) {
  completer.Reply(spi_.RegisterVmo(cs_, vmo_id, std::move(vmo), offset, size,
                                   SPI_VMO_RIGHT_READ | SPI_VMO_RIGHT_WRITE));
}

void SpiChild::UnregisterVmo(uint32_t vmo_id, UnregisterVmoCompleter::Sync& completer) {
  zx::vmo out_vmo;
  completer.Reply(spi_.UnregisterVmo(cs_, vmo_id, &out_vmo), std::move(out_vmo));
}

void SpiChild::TransmitVmo(uint32_t vmo_id, uint64_t offset, uint64_t size,
                           TransmitVmoCompleter::Sync& completer) {
  completer.Reply(spi_.TransmitVmo(cs_, vmo_id, offset, size));
}

void SpiChild::ReceiveVmo(uint32_t vmo_id, uint64_t offset, uint64_t size,
                          ReceiveVmoCompleter::Sync& completer) {
  completer.Reply(spi_.ReceiveVmo(cs_, vmo_id, offset, size));
}

void SpiChild::ExchangeVmo(uint32_t tx_vmo_id, uint64_t tx_offset, uint32_t rx_vmo_id,
                           uint64_t rx_offset, uint64_t size,
                           ExchangeVmoCompleter::Sync& completer) {
  completer.Reply(spi_.ExchangeVmo(cs_, tx_vmo_id, tx_offset, rx_vmo_id, rx_offset, size));
}

void SpiChild::RegisterVmoNew(uint32_t vmo_id, ::llcpp::fuchsia::mem::Range vmo,
                              sharedmemory::SharedVmoRight rights,
                              RegisterVmoNewCompleter::Sync& completer) {
  zx_status_t status = spi_.RegisterVmo(cs_, vmo_id, std::move(vmo.vmo), vmo.offset, vmo.size,
                                        static_cast<uint32_t>(rights));
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void SpiChild::UnregisterVmoNew(uint32_t vmo_id, UnregisterVmoNewCompleter::Sync& completer) {
  zx::vmo vmo;
  zx_status_t status = spi_.UnregisterVmo(cs_, vmo_id, &vmo);
  if (status == ZX_OK) {
    completer.ReplySuccess(std::move(vmo));
  } else {
    completer.ReplyError(status);
  }
}

void SpiChild::TransmitNew(sharedmemory::SharedVmoBuffer buffer,
                           TransmitNewCompleter::Sync& completer) {
  zx_status_t status = spi_.TransmitVmo(cs_, buffer.vmo_id, buffer.offset, buffer.size);
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void SpiChild::ReceiveNew(sharedmemory::SharedVmoBuffer buffer,
                          ReceiveNewCompleter::Sync& completer) {
  zx_status_t status = spi_.ReceiveVmo(cs_, buffer.vmo_id, buffer.offset, buffer.size);
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void SpiChild::ExchangeNew(sharedmemory::SharedVmoBuffer tx_buffer,
                           sharedmemory::SharedVmoBuffer rx_buffer,
                           ExchangeNewCompleter::Sync& completer) {
  if (tx_buffer.size != rx_buffer.size) {
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  zx_status_t status = spi_.ExchangeVmo(cs_, tx_buffer.vmo_id, tx_buffer.offset, rx_buffer.vmo_id,
                                        rx_buffer.offset, tx_buffer.size);
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

zx_status_t SpiChild::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  llcpp::fuchsia::hardware::spi::Device::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

zx_status_t SpiChild::SpiTransmit(const uint8_t* txdata_list, size_t txdata_count) {
  size_t actual;
  spi_.Exchange(cs_, txdata_list, txdata_count, nullptr, 0, &actual);
  return ZX_OK;
}
zx_status_t SpiChild::SpiReceive(uint32_t size, uint8_t* out_rxdata_list, size_t rxdata_count,
                                 size_t* out_rxdata_actual) {
  spi_.Exchange(cs_, nullptr, 0, out_rxdata_list, rxdata_count, out_rxdata_actual);
  return ZX_OK;
}

zx_status_t SpiChild::SpiExchange(const uint8_t* txdata_list, size_t txdata_count,
                                  uint8_t* out_rxdata_list, size_t rxdata_count,
                                  size_t* out_rxdata_actual) {
  spi_.Exchange(cs_, txdata_list, txdata_count, out_rxdata_list, rxdata_count, out_rxdata_actual);
  return ZX_OK;
}

void SpiChild::SpiConnectServer(zx::channel server) {
  spi_parent_.ConnectServer(std::move(server), this);
}

void SpiChild::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

void SpiChild::DdkRelease() { __UNUSED bool dummy = Release(); }

}  // namespace spi
