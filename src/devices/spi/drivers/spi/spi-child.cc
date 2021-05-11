// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spi-child.h"

#include <lib/ddk/trace/event.h>
#include <lib/trace-engine/types.h>

#include <ddktl/fidl.h>
#include <fbl/vector.h>

#include "spi.h"

namespace spi {

namespace sharedmemory = fuchsia_hardware_sharedmemory;

void SpiChild::TransmitVector(TransmitVectorRequestView request,
                              TransmitVectorCompleter::Sync& completer) {
  size_t rx_actual;
  zx_status_t status =
      spi_.Exchange(cs_, request->data.data(), request->data.count(), nullptr, 0, &rx_actual);
  if (status == ZX_OK) {
    completer.Reply(ZX_OK);
  } else {
    completer.Reply(status);
  }
}

void SpiChild::ReceiveVector(ReceiveVectorRequestView request,
                             ReceiveVectorCompleter::Sync& completer) {
  fbl::Vector<uint8_t> rxdata;
  rxdata.reserve(request->size);
  size_t rx_actual;
  zx_status_t status = spi_.Exchange(cs_, nullptr, 0, rxdata.begin(), request->size, &rx_actual);
  if (status == ZX_OK && rx_actual == request->size) {
    auto rx_vector = fidl::VectorView<uint8_t>::FromExternal(rxdata.data(), request->size);
    completer.Reply(ZX_OK, std::move(rx_vector));
  } else {
    completer.Reply(status == ZX_OK ? ZX_ERR_INTERNAL : status, fidl::VectorView<uint8_t>());
  }
}

void SpiChild::ExchangeVector(ExchangeVectorRequestView request,
                              ExchangeVectorCompleter::Sync& completer) {
  fbl::Vector<uint8_t> rxdata;
  const size_t size = request->txdata.count();
  rxdata.reserve(size);
  size_t rx_actual;
  zx_status_t status =
      spi_.Exchange(cs_, request->txdata.data(), size, rxdata.begin(), size, &rx_actual);
  if (status == ZX_OK && rx_actual == size) {
    auto rx_vector = fidl::VectorView<uint8_t>::FromExternal(rxdata.data(), size);
    completer.Reply(ZX_OK, std::move(rx_vector));
  } else {
    completer.Reply(status == ZX_OK ? ZX_ERR_INTERNAL : status, fidl::VectorView<uint8_t>());
  }
}

void SpiChild::RegisterVmo(RegisterVmoRequestView request, RegisterVmoCompleter::Sync& completer) {
  sharedmemory::wire::SharedVmoRegisterRegisterVmoResult result;
  sharedmemory::wire::SharedVmoRegisterRegisterVmoResponse response = {};
  zx_status_t status =
      spi_.RegisterVmo(cs_, request->vmo_id, std::move(request->vmo.vmo), request->vmo.offset,
                       request->vmo.size, static_cast<uint32_t>(request->rights));
  if (status == ZX_OK) {
    result.set_response(
        fidl::ObjectView<sharedmemory::wire::SharedVmoRegisterRegisterVmoResponse>::FromExternal(
            &response));
  } else {
    result.set_err(fidl::ObjectView<zx_status_t>::FromExternal(&status));
  }
  completer.Reply(std::move(result));
}

void SpiChild::UnregisterVmo(UnregisterVmoRequestView request,
                             UnregisterVmoCompleter::Sync& completer) {
  sharedmemory::wire::SharedVmoRegisterUnregisterVmoResult result;
  sharedmemory::wire::SharedVmoRegisterUnregisterVmoResponse response = {};
  zx_status_t status = spi_.UnregisterVmo(cs_, request->vmo_id, &response.vmo);
  if (status == ZX_OK) {
    result.set_response(
        fidl::ObjectView<sharedmemory::wire::SharedVmoRegisterUnregisterVmoResponse>::FromExternal(
            &response));
  } else {
    result.set_err(fidl::ObjectView<zx_status_t>::FromExternal(&status));
  }
  completer.Reply(std::move(result));
}

void SpiChild::Transmit(TransmitRequestView request, TransmitCompleter::Sync& completer) {
  sharedmemory::wire::SharedVmoIoTransmitResult result;
  sharedmemory::wire::SharedVmoIoTransmitResponse response = {};
  zx_status_t status;
  {
    TRACE_DURATION("spi", "Transmit", "cs", cs_, "size", request->buffer.size);
    status =
        spi_.TransmitVmo(cs_, request->buffer.vmo_id, request->buffer.offset, request->buffer.size);
  }
  if (status == ZX_OK) {
    result.set_response(
        fidl::ObjectView<sharedmemory::wire::SharedVmoIoTransmitResponse>::FromExternal(&response));
  } else {
    result.set_err(fidl::ObjectView<zx_status_t>::FromExternal(&status));
  }
  completer.Reply(std::move(result));
}

void SpiChild::Receive(ReceiveRequestView request, ReceiveCompleter::Sync& completer) {
  sharedmemory::wire::SharedVmoIoReceiveResult result;
  sharedmemory::wire::SharedVmoIoReceiveResponse response = {};
  zx_status_t status;
  {
    TRACE_DURATION("spi", "Receive", "cs", cs_, "size", request->buffer.size);
    status =
        spi_.ReceiveVmo(cs_, request->buffer.vmo_id, request->buffer.offset, request->buffer.size);
  }
  if (status == ZX_OK) {
    result.set_response(
        fidl::ObjectView<sharedmemory::wire::SharedVmoIoReceiveResponse>::FromExternal(&response));
  } else {
    result.set_err(fidl::ObjectView<zx_status_t>::FromExternal(&status));
  }
  completer.Reply(std::move(result));
}

void SpiChild::Exchange(ExchangeRequestView request, ExchangeCompleter::Sync& completer) {
  sharedmemory::wire::SharedVmoIoExchangeResult result;
  sharedmemory::wire::SharedVmoIoExchangeResponse response = {};

  zx_status_t status;
  if (request->tx_buffer.size != request->rx_buffer.size) {
    status = ZX_ERR_INVALID_ARGS;
  } else {
    TRACE_DURATION("spi", "Exchange", "cs", cs_, "size", request->tx_buffer.size);
    status = spi_.ExchangeVmo(cs_, request->tx_buffer.vmo_id, request->tx_buffer.offset,
                              request->rx_buffer.vmo_id, request->rx_buffer.offset,
                              request->tx_buffer.size);
  }

  if (status == ZX_OK) {
    result.set_response(
        fidl::ObjectView<sharedmemory::wire::SharedVmoIoExchangeResponse>::FromExternal(&response));
  } else {
    result.set_err(fidl::ObjectView<zx_status_t>::FromExternal(&status));
  }
  completer.Reply(std::move(result));
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
