// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/gpio_impl.fidl INSTEAD.

#pragma once

#include <ddk/protocol/gpio-impl.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_gpio_impl_protocol_config_in, GpioImplConfigIn,
                                     zx_status_t (C::*)(uint32_t index, uint32_t flags));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_gpio_impl_protocol_config_out, GpioImplConfigOut,
                                     zx_status_t (C::*)(uint32_t index, uint8_t initial_value));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_gpio_impl_protocol_set_alt_function,
                                     GpioImplSetAltFunction,
                                     zx_status_t (C::*)(uint32_t index, uint64_t function));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_gpio_impl_protocol_read, GpioImplRead,
                                     zx_status_t (C::*)(uint32_t index, uint8_t* out_value));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_gpio_impl_protocol_write, GpioImplWrite,
                                     zx_status_t (C::*)(uint32_t index, uint8_t value));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_gpio_impl_protocol_get_interrupt, GpioImplGetInterrupt,
                                     zx_status_t (C::*)(uint32_t index, uint32_t flags,
                                                        zx_handle_t* out_irq));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_gpio_impl_protocol_release_interrupt,
                                     GpioImplReleaseInterrupt, zx_status_t (C::*)(uint32_t index));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_gpio_impl_protocol_set_polarity, GpioImplSetPolarity,
                                     zx_status_t (C::*)(uint32_t index, gpio_polarity_t polarity));

template <typename D>
constexpr void CheckGpioImplProtocolSubclass() {
    static_assert(internal::has_gpio_impl_protocol_config_in<D>::value,
                  "GpioImplProtocol subclasses must implement "
                  "zx_status_t GpioImplConfigIn(uint32_t index, uint32_t flags");
    static_assert(internal::has_gpio_impl_protocol_config_out<D>::value,
                  "GpioImplProtocol subclasses must implement "
                  "zx_status_t GpioImplConfigOut(uint32_t index, uint8_t initial_value");
    static_assert(internal::has_gpio_impl_protocol_set_alt_function<D>::value,
                  "GpioImplProtocol subclasses must implement "
                  "zx_status_t GpioImplSetAltFunction(uint32_t index, uint64_t function");
    static_assert(internal::has_gpio_impl_protocol_read<D>::value,
                  "GpioImplProtocol subclasses must implement "
                  "zx_status_t GpioImplRead(uint32_t index, uint8_t* out_value");
    static_assert(internal::has_gpio_impl_protocol_write<D>::value,
                  "GpioImplProtocol subclasses must implement "
                  "zx_status_t GpioImplWrite(uint32_t index, uint8_t value");
    static_assert(
        internal::has_gpio_impl_protocol_get_interrupt<D>::value,
        "GpioImplProtocol subclasses must implement "
        "zx_status_t GpioImplGetInterrupt(uint32_t index, uint32_t flags, zx_handle_t* out_irq");
    static_assert(internal::has_gpio_impl_protocol_release_interrupt<D>::value,
                  "GpioImplProtocol subclasses must implement "
                  "zx_status_t GpioImplReleaseInterrupt(uint32_t index");
    static_assert(internal::has_gpio_impl_protocol_set_polarity<D>::value,
                  "GpioImplProtocol subclasses must implement "
                  "zx_status_t GpioImplSetPolarity(uint32_t index, gpio_polarity_t polarity");
}

} // namespace internal
} // namespace ddk
