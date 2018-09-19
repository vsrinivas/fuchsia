// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// flags for gpio_config_in()
#define GPIO_PULL_DOWN          (0 << 0)
#define GPIO_PULL_UP            (1 << 0)
#define GPIO_NO_PULL            (2 << 0)
#define GPIO_PULL_MASK          (3 << 0)

// Values for gpio_set_polarity()
#define GPIO_POLARITY_LOW       0
#define GPIO_POLARITY_HIGH      1

typedef struct {
    zx_status_t (*config_in)(void* ctx, uint32_t flags);
    zx_status_t (*config_out)(void* ctx, uint8_t initial_value);
    zx_status_t (*set_alt_function)(void* ctx, uint64_t function);
    zx_status_t (*read)(void* ctx, uint8_t* out_value);
    zx_status_t (*write)(void* ctx, uint8_t value);
    zx_status_t (*get_interrupt)(void* ctx, uint32_t flags, zx_handle_t* out_handle);
    zx_status_t (*release_interrupt)(void* ctx);
    zx_status_t (*set_polarity)(void* ctx, uint32_t polarity);
} gpio_protocol_ops_t;

typedef struct {
    gpio_protocol_ops_t* ops;
    void* ctx;
} gpio_protocol_t;

// configures the GPIO pin for input
static inline zx_status_t gpio_config_in(const gpio_protocol_t* gpio, uint32_t flags) {
    return gpio->ops->config_in(gpio->ctx, flags);
}

// configures the GPIO pin for output
static inline zx_status_t gpio_config_out(const gpio_protocol_t* gpio, uint8_t initial_value) {
    return gpio->ops->config_out(gpio->ctx, initial_value);
}

// configures the GPIO pin for an alternate function (I2C, SPI, etc)
// the interpretation of "function" is platform dependent
static inline zx_status_t gpio_set_alt_function(const gpio_protocol_t* gpio, uint64_t function) {
    return gpio->ops->set_alt_function(gpio->ctx, function);
}

// reads the current value of the GPIO pin (0 or 1)
static inline zx_status_t gpio_read(const gpio_protocol_t* gpio, uint8_t* out_value) {
    return gpio->ops->read(gpio->ctx, out_value);
}

// sets the current value of the GPIO pin (any non-zero value maps to 1)
static inline zx_status_t gpio_write(const gpio_protocol_t* gpio, uint8_t value) {
    return gpio->ops->write(gpio->ctx, value);
}

// gets an interrupt object pertaining to the GPIO pin
static inline zx_status_t gpio_get_interrupt(const gpio_protocol_t* gpio, uint32_t flags,
                                             zx_handle_t* out_handle) {
    return gpio->ops->get_interrupt(gpio->ctx, flags, out_handle);
}

// release the interrupt
static inline zx_status_t gpio_release_interrupt(const gpio_protocol_t* gpio) {
    return gpio->ops->release_interrupt(gpio->ctx);
}

// Set GPIO polarity
static inline zx_status_t gpio_set_polarity(const gpio_protocol_t* gpio, uint32_t polarity) {
    return gpio->ops->set_polarity(gpio->ctx, polarity);
}
__END_CDECLS;
