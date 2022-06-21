// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the fuchsia.hardware.spi banjo file

#pragma once

#include <ddktl/device-internal.h>
#include <fuchsia/hardware/spi/c/banjo.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/zx/channel.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "banjo-internal.h"

// DDK spi-protocol support
//
// :: Proxies ::
//
// ddk::SpiProtocolClient is a simple wrapper around
// spi_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::SpiProtocol is a mixin class that simplifies writing DDK drivers
// that implement the spi protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_SPI device.
// class SpiDevice;
// using SpiDeviceType = ddk::Device<SpiDevice, /* ddk mixins */>;
//
// class SpiDevice : public SpiDeviceType,
//                      public ddk::SpiProtocol<SpiDevice> {
//   public:
//     SpiDevice(zx_device_t* parent)
//         : SpiDeviceType(parent) {}
//
//     zx_status_t SpiTransmit(const uint8_t* txdata_list, size_t txdata_count);
//
//     zx_status_t SpiReceive(uint32_t size, uint8_t* out_rxdata_list, size_t rxdata_count, size_t* out_rxdata_actual);
//
//     zx_status_t SpiExchange(const uint8_t* txdata_list, size_t txdata_count, uint8_t* out_rxdata_list, size_t rxdata_count, size_t* out_rxdata_actual);
//
//     void SpiConnectServer(zx::channel server);
//
//     ...
// };

namespace ddk {

template <typename D, typename Base = internal::base_mixin>
class SpiProtocol : public Base {
public:
    SpiProtocol() {
        internal::CheckSpiProtocolSubclass<D>();
        spi_protocol_ops_.transmit = SpiTransmit;
        spi_protocol_ops_.receive = SpiReceive;
        spi_protocol_ops_.exchange = SpiExchange;
        spi_protocol_ops_.connect_server = SpiConnectServer;

        if constexpr (internal::is_base_proto<Base>::value) {
            auto dev = static_cast<D*>(this);
            // Can only inherit from one base_protocol implementation.
            ZX_ASSERT(dev->ddk_proto_id_ == 0);
            dev->ddk_proto_id_ = ZX_PROTOCOL_SPI;
            dev->ddk_proto_ops_ = &spi_protocol_ops_;
        }
    }

protected:
    spi_protocol_ops_t spi_protocol_ops_ = {};

private:
    // Half-duplex transmit data to a SPI device; always transmits the entire buffer on success.
    static zx_status_t SpiTransmit(void* ctx, const uint8_t* txdata_list, size_t txdata_count) {
        auto ret = static_cast<D*>(ctx)->SpiTransmit(txdata_list, txdata_count);
        return ret;
    }
    // Half-duplex receive data from a SPI device; always reads the full size requested.
    static zx_status_t SpiReceive(void* ctx, uint32_t size, uint8_t* out_rxdata_list, size_t rxdata_count, size_t* out_rxdata_actual) {
        auto ret = static_cast<D*>(ctx)->SpiReceive(size, out_rxdata_list, rxdata_count, out_rxdata_actual);
        return ret;
    }
    // Full-duplex SPI transaction. Received data will exactly equal the length of the transmit
    // buffer.
    static zx_status_t SpiExchange(void* ctx, const uint8_t* txdata_list, size_t txdata_count, uint8_t* out_rxdata_list, size_t rxdata_count, size_t* out_rxdata_actual) {
        auto ret = static_cast<D*>(ctx)->SpiExchange(txdata_list, txdata_count, out_rxdata_list, rxdata_count, out_rxdata_actual);
        return ret;
    }
    // Tells the SPI driver to start listening for fuchsia.hardware.spi messages on server.
    // See sdk/fidl/fuchsia.hardware.spi/spi.fidl.
    static void SpiConnectServer(void* ctx, zx_handle_t server) {
        static_cast<D*>(ctx)->SpiConnectServer(zx::channel(server));
    }
};

class SpiProtocolClient {
public:
    SpiProtocolClient()
        : ops_(nullptr), ctx_(nullptr) {}
    SpiProtocolClient(const spi_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    SpiProtocolClient(zx_device_t* parent) {
        spi_protocol_t proto;
        if (device_get_protocol(parent, ZX_PROTOCOL_SPI, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    SpiProtocolClient(zx_device_t* parent, const char* fragment_name) {
        spi_protocol_t proto;
        if (device_get_fragment_protocol(parent, fragment_name, ZX_PROTOCOL_SPI, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    // Create a SpiProtocolClient from the given parent device + "fragment".
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent,
                                        SpiProtocolClient* result) {
        spi_protocol_t proto;
        zx_status_t status = device_get_protocol(
                parent, ZX_PROTOCOL_SPI, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = SpiProtocolClient(&proto);
        return ZX_OK;
    }

    // Create a SpiProtocolClient from the given parent device.
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent, const char* fragment_name,
                                        SpiProtocolClient* result) {
        spi_protocol_t proto;
        zx_status_t status = device_get_fragment_protocol(parent, fragment_name,
                                 ZX_PROTOCOL_SPI, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = SpiProtocolClient(&proto);
        return ZX_OK;
    }

    void GetProto(spi_protocol_t* proto) const {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() const {
        return ops_ != nullptr;
    }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }

    // Half-duplex transmit data to a SPI device; always transmits the entire buffer on success.
    zx_status_t Transmit(const uint8_t* txdata_list, size_t txdata_count) const {
        return ops_->transmit(ctx_, txdata_list, txdata_count);
    }

    // Half-duplex receive data from a SPI device; always reads the full size requested.
    zx_status_t Receive(uint32_t size, uint8_t* out_rxdata_list, size_t rxdata_count, size_t* out_rxdata_actual) const {
        return ops_->receive(ctx_, size, out_rxdata_list, rxdata_count, out_rxdata_actual);
    }

    // Full-duplex SPI transaction. Received data will exactly equal the length of the transmit
    // buffer.
    zx_status_t Exchange(const uint8_t* txdata_list, size_t txdata_count, uint8_t* out_rxdata_list, size_t rxdata_count, size_t* out_rxdata_actual) const {
        return ops_->exchange(ctx_, txdata_list, txdata_count, out_rxdata_list, rxdata_count, out_rxdata_actual);
    }

    // Tells the SPI driver to start listening for fuchsia.hardware.spi messages on server.
    // See sdk/fidl/fuchsia.hardware.spi/spi.fidl.
    void ConnectServer(zx::channel server) const {
        ops_->connect_server(ctx_, server.release());
    }

private:
    spi_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
