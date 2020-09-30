// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spi-child.h"

#include <ddktl/fidl.h>
#include <fbl/vector.h>

namespace spi {

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

zx_status_t SpiChild::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
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

void SpiChild::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

void SpiChild::DdkRelease() { __UNUSED bool dummy = Release(); }

}  // namespace spi
