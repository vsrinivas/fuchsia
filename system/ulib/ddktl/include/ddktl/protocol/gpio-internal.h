// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_gpio_config_in, GpioConfigIn,
        zx_status_t (C::*)(uint32_t));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_gpio_config_out, GpioConfigOut,
        zx_status_t (C::*)(uint8_t));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_gpio_set_alt_function, GpioSetAltFunction,
        zx_status_t (C::*)(uint64_t));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_gpio_read, GpioRead,
        zx_status_t (C::*)(uint8_t*));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_gpio_write, GpioWrite,
        zx_status_t (C::*)(uint8_t));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_gpio_get_interrupt, GpioGetInterrupt,
        zx_status_t (C::*)(uint32_t, zx_handle_t*));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_gpio_release_interrupt, GpioReleaseInterrupt,
        zx_status_t (C::*)());
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_gpio_set_polarity, GpioSetPolarity,
        zx_status_t (C::*)(uint32_t));

template <typename D>
constexpr void CheckGpioProtocolSubclass() {
    static_assert(internal::has_gpio_config_in<D>::value,
                  "GpioProtocol subclasses must implement "
                  "GpioConfigIn(uint32_t flags)");
    static_assert(internal::has_gpio_config_out<D>::value,
                  "GpioProtocol subclasses must implement "
                  "GpioConfigOut(uint8_t initial_value)");
    static_assert(internal::has_gpio_set_alt_function<D>::value,
                  "GpioProtocol subclasses must implement "
                  "GpioSetAltFunction(uint64_t function)");
    static_assert(internal::has_gpio_read<D>::value,
                  "GpioProtocol subclasses must implement "
                  "GpioRead(uint8_t* out_value)");
    static_assert(internal::has_gpio_write<D>::value,
                  "GpioProtocol subclasses must implement "
                  "GpioWrite(uint8_t value)");
    static_assert(internal::has_gpio_get_interrupt<D>::value,
                  "GpioProtocol subclasses must implement "
                  "GpioGetInterrupt(uint32_t flags, zx_handle_t* out_handle)");
    static_assert(internal::has_gpio_release_interrupt<D>::value,
                  "GpioProtocol subclasses must implement "
                  "GpioReleaseInterrupt()");
    static_assert(internal::has_gpio_set_polarity<D>::value,
                  "GpioProtocol subclasses must implement "
                  "GpioSetPolarity(uint32_t polarity)");
 }

}  // namespace internal
}  // namespace ddk
