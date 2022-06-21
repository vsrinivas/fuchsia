// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the fuchsia.hardware.spi banjo file

#pragma once

#include <type_traits>

namespace ddk {
namespace internal {

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_spi_protocol_transmit, SpiTransmit,
        zx_status_t (C::*)(const uint8_t* txdata_list, size_t txdata_count));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_spi_protocol_receive, SpiReceive,
        zx_status_t (C::*)(uint32_t size, uint8_t* out_rxdata_list, size_t rxdata_count, size_t* out_rxdata_actual));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_spi_protocol_exchange, SpiExchange,
        zx_status_t (C::*)(const uint8_t* txdata_list, size_t txdata_count, uint8_t* out_rxdata_list, size_t rxdata_count, size_t* out_rxdata_actual));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_spi_protocol_connect_server, SpiConnectServer,
        void (C::*)(zx::channel server));


template <typename D>
constexpr void CheckSpiProtocolSubclass() {
    static_assert(internal::has_spi_protocol_transmit<D>::value,
        "SpiProtocol subclasses must implement "
        "zx_status_t SpiTransmit(const uint8_t* txdata_list, size_t txdata_count);");

    static_assert(internal::has_spi_protocol_receive<D>::value,
        "SpiProtocol subclasses must implement "
        "zx_status_t SpiReceive(uint32_t size, uint8_t* out_rxdata_list, size_t rxdata_count, size_t* out_rxdata_actual);");

    static_assert(internal::has_spi_protocol_exchange<D>::value,
        "SpiProtocol subclasses must implement "
        "zx_status_t SpiExchange(const uint8_t* txdata_list, size_t txdata_count, uint8_t* out_rxdata_list, size_t rxdata_count, size_t* out_rxdata_actual);");

    static_assert(internal::has_spi_protocol_connect_server<D>::value,
        "SpiProtocol subclasses must implement "
        "void SpiConnectServer(zx::channel server);");

}


} // namespace internal
} // namespace ddk
