// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// flags for gpio_config()
enum {
    GPIO_DIR_IN             = 0 << 0,
    GPIO_DIR_OUT            = 1 << 0,
    GPIO_DIR_MASK           = 1 << 0,

    GPIO_TRIGGER_EDGE       = 0 << 1,
    GPIO_TRIGGER_LEVEL      = 1 << 1,
    GPIO_TRIGGER_MASK       = 1 << 1,

    // for edge triggered
    GPIO_TRIGGER_RISING     = 1 << 2,
    GPIO_TRIGGER_FALLING    = 1 << 3,

    // for level triggered
    GPIO_TRIGGER_HIGH       = 1 << 2,
    GPIO_TRIGGER_LOW        = 1 << 3,

    // for pull-up/pull-down/no-pull
    GPIO_PULL_DOWN          = 0 << 4,
    GPIO_PULL_UP            = 1 << 4,
    GPIO_NO_PULL            = 2 << 4,
    GPIO_PULL_MASK          = 3 << 4,
};

enum
{
    GPIO_POLARITY_LOW =     0,
    GPIO_POLARITY_HIGH =    1,
};

// In the functions below, the GPIO index is relative to the list of GPIOs for the device.
// For example, the list of GPIOs a platform device has access to would likely be a small
// subset of the total number of GPIOs, while a platform bus implementation driver would
// have access to the complete set of GPIOs.

typedef struct {
    zx_status_t (*config)(void* ctx, uint32_t index, uint32_t flags);
    zx_status_t (*set_alt_function)(void* ctx, uint32_t index, uint64_t function);
    zx_status_t (*read)(void* ctx, uint32_t index, uint8_t* out_value);
    zx_status_t (*write)(void* ctx, uint32_t index, uint8_t value);
    zx_status_t (*get_interrupt)(void* ctx, uint32_t pin, uint32_t flags, zx_handle_t* out_handle);
    zx_status_t (*release_interrupt)(void* ctx, uint32_t pin);
    zx_status_t (*set_polarity)(void* ctx, uint32_t pin, uint32_t polarity);
} gpio_protocol_ops_t;

typedef struct {
    gpio_protocol_ops_t* ops;
    void* ctx;
} gpio_protocol_t;

// configures a GPIO
static inline zx_status_t gpio_config(gpio_protocol_t* gpio, uint32_t index,
                                      uint32_t flags) {
    return gpio->ops->config(gpio->ctx, index, flags);
}

// configures the GPIO pin for an alternate function (I2C, SPI, etc)
// the interpretation of "function" is platform dependent
static inline zx_status_t gpio_set_alt_function(gpio_protocol_t* gpio, uint32_t index,
                                                uint64_t function) {
    return gpio->ops->set_alt_function(gpio->ctx, index, function);
}

// reads the current value of a GPIO (0 or 1)
static inline zx_status_t gpio_read(gpio_protocol_t* gpio, uint32_t index, uint8_t* out_value) {
    return gpio->ops->read(gpio->ctx, index, out_value);
}

// sets the current value of the GPIO (any non-zero value maps to 1)
static inline zx_status_t gpio_write(gpio_protocol_t* gpio, uint32_t index, uint8_t value) {
    return gpio->ops->write(gpio->ctx, index, value);
}

// gets an interrupt object pertaining to a particular GPIO pin
static inline zx_status_t gpio_get_interrupt(gpio_protocol_t* gpio, uint32_t index,
                                             uint32_t flags, zx_handle_t* out_handle) {
    return gpio->ops->get_interrupt(gpio->ctx, index, flags, out_handle);
}

// release the interrupt
static inline zx_status_t gpio_release_interrupt(gpio_protocol_t* gpio, uint32_t pin) {
    return gpio->ops->release_interrupt(gpio->ctx, pin);
}

// Set GPIO polarity
static inline zx_status_t gpio_set_polarity(gpio_protocol_t* gpio, uint32_t pin,
                                            uint32_t polarity) {
    return gpio->ops->set_polarity(gpio->ctx, pin, polarity);
}
__END_CDECLS;
