// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_gpio_impl_config_in, GpioImplConfigIn,
        zx_status_t (C::*)(uint32_t, uint32_t));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_gpio_impl_config_out, GpioImplConfigOut,
        zx_status_t (C::*)(uint32_t, uint8_t));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_gpio_impl_set_alt_function, GpioImplSetAltFunction,
        zx_status_t (C::*)(uint32_t, uint64_t));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_gpio_impl_read, GpioImplRead,
        zx_status_t (C::*)(uint32_t, uint8_t*));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_gpio_impl_write, GpioImplWrite,
        zx_status_t (C::*)(uint32_t, uint8_t));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_gpio_impl_get_interrupt, GpioImplGetInterrupt,
        zx_status_t (C::*)(uint32_t, uint32_t, zx_handle_t*));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_gpio_impl_release_interrupt, GpioImplReleaseInterrupt,
        zx_status_t (C::*)(uint32_t));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_gpio_impl_set_polarity, GpioImplSetPolarity,
        zx_status_t (C::*)(uint32_t, uint32_t));

template <typename D>
constexpr void CheckGpioImplProtocolSubclass() {
    static_assert(internal::has_gpio_impl_config_in<D>::value,
                  "GpioImplProtocol subclasses must implement "
                  "GpioImplConfigIn(uint32_t index, uint32_t flags)");
    static_assert(internal::has_gpio_impl_config_out<D>::value,
                  "GpioImplProtocol subclasses must implement "
                  "GpioImplConfigOut(uint32_t index, uint8_t initial_value)");
    static_assert(internal::has_gpio_impl_set_alt_function<D>::value,
                  "GpioImplProtocol subclasses must implement "
                  "GpioImplSetAltFunction(uint32_t index, uint64_t function)");
    static_assert(internal::has_gpio_impl_read<D>::value,
                  "GpioImplProtocol subclasses must implement "
                  "GpioImplRead(uint32_t index, uint8_t* out_value)");
    static_assert(internal::has_gpio_impl_write<D>::value,
                  "GpioImplProtocol subclasses must implement "
                  "GpioImplWrite(uint32_t index, uint8_t value)");
    static_assert(internal::has_gpio_impl_get_interrupt<D>::value,
                  "GpioImplProtocol subclasses must implement "
                  "GpioImplGetInterrupt(uint32_t index, uint32_t flags, zx_handle_t* out_handle)");
    static_assert(internal::has_gpio_impl_release_interrupt<D>::value,
                  "GpioImplProtocol subclasses must implement "
                  "GpioImplReleaseInterrupt(uint32_t index)");
    static_assert(internal::has_gpio_impl_set_polarity<D>::value,
                  "GpioImplProtocol subclasses must implement "
                  "GpioImplSetPolarity(uint32_t index, uint32_t polarity)");
 }

}  // namespace internal
}  // namespace ddk
