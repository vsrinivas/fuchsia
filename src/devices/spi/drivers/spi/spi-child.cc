// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spi-child.h"

#include <ddktl/fidl.h>
#include <fbl/vector.h>

#include "spi.h"

namespace spi {

namespace sharedmemory = fuchsia_hardware_sharedmemory;

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

void SpiChild::RegisterVmo(uint32_t vmo_id, fuchsia_mem::wire::Range vmo,
                           sharedmemory::wire::SharedVmoRight rights,
                           RegisterVmoCompleter::Sync& completer) {
  sharedmemory::wire::SharedVmoRegister_RegisterVmo_Result result;
  fidl::aligned<sharedmemory::wire::SharedVmoRegister_RegisterVmo_Response> response = {};
  zx_status_t status = spi_.RegisterVmo(cs_, vmo_id, std::move(vmo.vmo), vmo.offset, vmo.size,
                                        static_cast<uint32_t>(rights));
  if (status == ZX_OK) {
    result.set_response(fidl::unowned_ptr(&response));
  } else {
    result.set_err(fidl::unowned_ptr(&status));
  }
  completer.Reply(std::move(result));
}

void SpiChild::UnregisterVmo(uint32_t vmo_id, UnregisterVmoCompleter::Sync& completer) {
  sharedmemory::wire::SharedVmoRegister_UnregisterVmo_Result result;
  sharedmemory::wire::SharedVmoRegister_UnregisterVmo_Response response = {};
  zx_status_t status = spi_.UnregisterVmo(cs_, vmo_id, &response.vmo);
  if (status == ZX_OK) {
    result.set_response(fidl::unowned_ptr(&response));
  } else {
    result.set_err(fidl::unowned_ptr(&status));
  }
  completer.Reply(std::move(result));
}

void SpiChild::Transmit(sharedmemory::wire::SharedVmoBuffer buffer,
                        TransmitCompleter::Sync& completer) {
  sharedmemory::wire::SharedVmoIo_Transmit_Result result;
  fidl::aligned<sharedmemory::wire::SharedVmoIo_Transmit_Response> response = {};
  zx_status_t status = spi_.TransmitVmo(cs_, buffer.vmo_id, buffer.offset, buffer.size);
  if (status == ZX_OK) {
    result.set_response(fidl::unowned_ptr(&response));
  } else {
    result.set_err(fidl::unowned_ptr(&status));
  }
  completer.Reply(std::move(result));
}

void SpiChild::Receive(sharedmemory::wire::SharedVmoBuffer buffer,
                       ReceiveCompleter::Sync& completer) {
  sharedmemory::wire::SharedVmoIo_Receive_Result result;
  fidl::aligned<sharedmemory::wire::SharedVmoIo_Receive_Response> response = {};
  zx_status_t status = spi_.ReceiveVmo(cs_, buffer.vmo_id, buffer.offset, buffer.size);
  if (status == ZX_OK) {
    result.set_response(fidl::unowned_ptr(&response));
  } else {
    result.set_err(fidl::unowned_ptr(&status));
  }
  completer.Reply(std::move(result));
}

void SpiChild::Exchange(sharedmemory::wire::SharedVmoBuffer tx_buffer,
                        sharedmemory::wire::SharedVmoBuffer rx_buffer,
                        ExchangeCompleter::Sync& completer) {
  sharedmemory::wire::SharedVmoIo_Exchange_Result result;
  fidl::aligned<sharedmemory::wire::SharedVmoIo_Exchange_Response> response = {};

  zx_status_t status;
  if (tx_buffer.size != rx_buffer.size) {
    status = ZX_ERR_INVALID_ARGS;
  } else {
    status = spi_.ExchangeVmo(cs_, tx_buffer.vmo_id, tx_buffer.offset, rx_buffer.vmo_id,
                              rx_buffer.offset, tx_buffer.size);
  }

  if (status == ZX_OK) {
    result.set_response(fidl::unowned_ptr(&response));
  } else {
    result.set_err(fidl::unowned_ptr(&status));
  }
  completer.Reply(std::move(result));
}

zx_status_t SpiChild::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  fuchsia_hardware_spi::Device::Dispatch(this, msg, &transaction);
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
