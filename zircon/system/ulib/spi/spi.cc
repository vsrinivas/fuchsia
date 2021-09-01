// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/lib/spi/spi.h"

#include <fidl/fuchsia.hardware.spi/cpp/wire.h>

__BEGIN_CDECLS

zx_status_t spilib_transmit(zx_handle_t channel, void* data, size_t length) {
  auto result = fidl::WireCall(fidl::UnownedClientEnd<fuchsia_hardware_spi::Device>(channel))
                    .TransmitVector(fidl::VectorView<uint8_t>::FromExternal(
                        reinterpret_cast<uint8_t*>(data), length));
  if (result.status() != ZX_OK) {
    return result.status();
  }
  if (result->status != ZX_OK) {
    return result->status;
  }
  return ZX_OK;
}

zx_status_t spilib_receive(zx_handle_t channel, void* data, size_t length) {
  auto result = fidl::WireCall(fidl::UnownedClientEnd<fuchsia_hardware_spi::Device>(channel))
                    .ReceiveVector(length);
  if (result.status() != ZX_OK) {
    return result.status();
  }
  if (result->status != ZX_OK) {
    return result->status;
  }
  memcpy(data, result->data.data(), result->data.count());
  return ZX_OK;
}

zx_status_t spilib_exchange(zx_handle_t channel, void* txdata, void* rxdata, size_t length) {
  auto result = fidl::WireCall(fidl::UnownedClientEnd<fuchsia_hardware_spi::Device>(channel))
                    .ExchangeVector(fidl::VectorView<uint8_t>::FromExternal(
                        reinterpret_cast<uint8_t*>(txdata), length));
  if (result.status() != ZX_OK) {
    return result.status();
  }
  if (result->status != ZX_OK) {
    return result->status;
  }
  memcpy(rxdata, result->rxdata.data(), result->rxdata.count());
  return ZX_OK;
}

__END_CDECLS
