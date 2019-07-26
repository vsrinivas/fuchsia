// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DDK_PROTOCOL_SPI_LIB_H_
#define DDK_PROTOCOL_SPI_LIB_H_

#include <fuchsia/hardware/spi/c/fidl.h>
#include <stdio.h>

__BEGIN_CDECLS

static inline zx_status_t spi_transmit(zx_handle_t channel, const void* data, size_t length) {
  zx_status_t status;
  zx_status_t call_status =
      fuchsia_hardware_spi_DeviceTransmit(channel, (const uint8_t*)data, length, &status);
  if (call_status != ZX_OK) {
    return call_status;
  }
  return status;
}

static inline zx_status_t spi_receive(zx_handle_t channel, void* data, size_t length) {
  zx_status_t status;
  size_t actual;
  zx_status_t call_status = fuchsia_hardware_spi_DeviceReceive(channel, (uint32_t)length, &status,
                                                               (uint8_t*)data, length, &actual);
  if (call_status != ZX_OK) {
    return call_status;
  }
  return status;
}

static inline zx_status_t spi_exchange(zx_handle_t channel, const void* txdata, void* rxdata,
                                       size_t length) {
  zx_status_t status;
  size_t actual;
  zx_status_t call_status = fuchsia_hardware_spi_DeviceExchange(
      channel, (const uint8_t*)txdata, length, &status, (uint8_t*)rxdata, length, &actual);
  if (call_status != ZX_OK) {
    return call_status;
  }
  return status;
}

__END_CDECLS

#endif  // DDK_PROTOCOL_SPI_LIB_H_
