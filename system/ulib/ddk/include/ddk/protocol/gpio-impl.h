// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/gpio_impl.banjo INSTEAD.

#pragma once

#include <ddk/protocol/gpio.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Forward declarations

typedef struct gpio_impl_protocol gpio_impl_protocol_t;

// Declarations

typedef struct gpio_impl_protocol_ops {
    zx_status_t (*config_in)(void* ctx, uint32_t index, uint32_t flags);
    zx_status_t (*config_out)(void* ctx, uint32_t index, uint8_t initial_value);
    zx_status_t (*set_alt_function)(void* ctx, uint32_t index, uint64_t function);
    zx_status_t (*read)(void* ctx, uint32_t index, uint8_t* out_value);
    zx_status_t (*write)(void* ctx, uint32_t index, uint8_t value);
    zx_status_t (*get_interrupt)(void* ctx, uint32_t index, uint32_t flags, zx_handle_t* out_irq);
    zx_status_t (*release_interrupt)(void* ctx, uint32_t index);
    zx_status_t (*set_polarity)(void* ctx, uint32_t index, gpio_polarity_t polarity);
} gpio_impl_protocol_ops_t;

struct gpio_impl_protocol {
    gpio_impl_protocol_ops_t* ops;
    void* ctx;
};

// Configures a GPIO for input.
static inline zx_status_t gpio_impl_config_in(const gpio_impl_protocol_t* proto, uint32_t index,
                                              uint32_t flags) {
    return proto->ops->config_in(proto->ctx, index, flags);
}
// Configures a GPIO for output.
static inline zx_status_t gpio_impl_config_out(const gpio_impl_protocol_t* proto, uint32_t index,
                                               uint8_t initial_value) {
    return proto->ops->config_out(proto->ctx, index, initial_value);
}
// Configures the GPIO pin for an alternate function (I2C, SPI, etc)
// the interpretation of "function" is platform dependent.
static inline zx_status_t gpio_impl_set_alt_function(const gpio_impl_protocol_t* proto,
                                                     uint32_t index, uint64_t function) {
    return proto->ops->set_alt_function(proto->ctx, index, function);
}
// Reads the current value of a GPIO (0 or 1).
static inline zx_status_t gpio_impl_read(const gpio_impl_protocol_t* proto, uint32_t index,
                                         uint8_t* out_value) {
    return proto->ops->read(proto->ctx, index, out_value);
}
// Sets the current value of the GPIO (any non-zero value maps to 1).
static inline zx_status_t gpio_impl_write(const gpio_impl_protocol_t* proto, uint32_t index,
                                          uint8_t value) {
    return proto->ops->write(proto->ctx, index, value);
}
// Gets an interrupt object pertaining to a particular GPIO pin.
static inline zx_status_t gpio_impl_get_interrupt(const gpio_impl_protocol_t* proto, uint32_t index,
                                                  uint32_t flags, zx_handle_t* out_irq) {
    return proto->ops->get_interrupt(proto->ctx, index, flags, out_irq);
}
// Release the interrupt.
static inline zx_status_t gpio_impl_release_interrupt(const gpio_impl_protocol_t* proto,
                                                      uint32_t index) {
    return proto->ops->release_interrupt(proto->ctx, index);
}
// Set GPIO polarity.
static inline zx_status_t gpio_impl_set_polarity(const gpio_impl_protocol_t* proto, uint32_t index,
                                                 gpio_polarity_t polarity) {
    return proto->ops->set_polarity(proto->ctx, index, polarity);
}

__END_CDECLS;
