// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SPI_SPI_H_
#define LIB_SPI_SPI_H_

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

zx_status_t spilib_transmit(zx_handle_t channel, void* data, size_t length);

zx_status_t spilib_receive(zx_handle_t channel, void* data, size_t length);

zx_status_t spilib_exchange(zx_handle_t channel, void* txdata, void* rxdata, size_t length);

__END_CDECLS

#endif  // LIB_SPI_SPI_H_
