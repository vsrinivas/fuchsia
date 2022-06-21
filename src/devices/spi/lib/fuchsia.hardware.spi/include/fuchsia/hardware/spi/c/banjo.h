// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the fuchsia.hardware.spi banjo file

#pragma once


#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Forward declarations
typedef struct spi_protocol spi_protocol_t;
typedef struct spi_protocol_ops spi_protocol_ops_t;

// Declarations
struct spi_protocol_ops {
    zx_status_t (*transmit)(void* ctx, const uint8_t* txdata_list, size_t txdata_count);
    zx_status_t (*receive)(void* ctx, uint32_t size, uint8_t* out_rxdata_list, size_t rxdata_count, size_t* out_rxdata_actual);
    zx_status_t (*exchange)(void* ctx, const uint8_t* txdata_list, size_t txdata_count, uint8_t* out_rxdata_list, size_t rxdata_count, size_t* out_rxdata_actual);
    void (*connect_server)(void* ctx, zx_handle_t server);
};


struct spi_protocol {
    spi_protocol_ops_t* ops;
    void* ctx;
};


// Helpers
// Half-duplex transmit data to a SPI device; always transmits the entire buffer on success.
static inline zx_status_t spi_transmit(const spi_protocol_t* proto, const uint8_t* txdata_list, size_t txdata_count) {
    return proto->ops->transmit(proto->ctx, txdata_list, txdata_count);
}

// Half-duplex receive data from a SPI device; always reads the full size requested.
static inline zx_status_t spi_receive(const spi_protocol_t* proto, uint32_t size, uint8_t* out_rxdata_list, size_t rxdata_count, size_t* out_rxdata_actual) {
    return proto->ops->receive(proto->ctx, size, out_rxdata_list, rxdata_count, out_rxdata_actual);
}

// Full-duplex SPI transaction. Received data will exactly equal the length of the transmit
// buffer.
static inline zx_status_t spi_exchange(const spi_protocol_t* proto, const uint8_t* txdata_list, size_t txdata_count, uint8_t* out_rxdata_list, size_t rxdata_count, size_t* out_rxdata_actual) {
    return proto->ops->exchange(proto->ctx, txdata_list, txdata_count, out_rxdata_list, rxdata_count, out_rxdata_actual);
}

// Tells the SPI driver to start listening for fuchsia.hardware.spi messages on server.
// See sdk/fidl/fuchsia.hardware.spi/spi.fidl.
static inline void spi_connect_server(const spi_protocol_t* proto, zx_handle_t server) {
    proto->ops->connect_server(proto->ctx, server);
}


__END_CDECLS
