// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/gpio.banjo INSTEAD.

#pragma once

#include <ddk/protocol/gpio.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_gpio_protocol_config_in, GpioConfigIn,
                                     zx_status_t (C::*)(uint32_t flags));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_gpio_protocol_config_out, GpioConfigOut,
                                     zx_status_t (C::*)(uint8_t initial_value));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_gpio_protocol_set_alt_function, GpioSetAltFunction,
                                     zx_status_t (C::*)(uint64_t function));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_gpio_protocol_read, GpioRead,
                                     zx_status_t (C::*)(uint8_t* out_value));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_gpio_protocol_write, GpioWrite,
                                     zx_status_t (C::*)(uint8_t value));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_gpio_protocol_get_interrupt, GpioGetInterrupt,
                                     zx_status_t (C::*)(uint32_t flags, zx_handle_t* out_irq));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_gpio_protocol_release_interrupt, GpioReleaseInterrupt,
                                     zx_status_t (C::*)());
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_gpio_protocol_set_polarity, GpioSetPolarity,
                                     zx_status_t (C::*)(gpio_polarity_t polarity));

template <typename D>
constexpr void CheckGpioProtocolSubclass() {
    static_assert(internal::has_gpio_protocol_config_in<D>::value,
                  "GpioProtocol subclasses must implement "
                  "zx_status_t GpioConfigIn(uint32_t flags");
    static_assert(internal::has_gpio_protocol_config_out<D>::value,
                  "GpioProtocol subclasses must implement "
                  "zx_status_t GpioConfigOut(uint8_t initial_value");
    static_assert(internal::has_gpio_protocol_set_alt_function<D>::value,
                  "GpioProtocol subclasses must implement "
                  "zx_status_t GpioSetAltFunction(uint64_t function");
    static_assert(internal::has_gpio_protocol_read<D>::value,
                  "GpioProtocol subclasses must implement "
                  "zx_status_t GpioRead(uint8_t* out_value");
    static_assert(internal::has_gpio_protocol_write<D>::value,
                  "GpioProtocol subclasses must implement "
                  "zx_status_t GpioWrite(uint8_t value");
    static_assert(internal::has_gpio_protocol_get_interrupt<D>::value,
                  "GpioProtocol subclasses must implement "
                  "zx_status_t GpioGetInterrupt(uint32_t flags, zx_handle_t* out_irq");
    static_assert(internal::has_gpio_protocol_release_interrupt<D>::value,
                  "GpioProtocol subclasses must implement "
                  "zx_status_t GpioReleaseInterrupt(");
    static_assert(internal::has_gpio_protocol_set_polarity<D>::value,
                  "GpioProtocol subclasses must implement "
                  "zx_status_t GpioSetPolarity(gpio_polarity_t polarity");
}

} // namespace internal
} // namespace ddk
