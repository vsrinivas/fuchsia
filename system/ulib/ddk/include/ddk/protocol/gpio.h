// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/types.h>

__BEGIN_CDECLS;

// flags for gpio_config()
typedef enum {
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
} gpio_config_flags_t;

typedef struct {
    mx_status_t (*config)(void* ctx, unsigned pin, gpio_config_flags_t flags);
    mx_status_t (*read)(void* ctx, unsigned pin, unsigned* out_value);
    mx_status_t (*write)(void* ctx, unsigned pin, unsigned value);
    mx_status_t (*int_enable)(void* ctx, unsigned pin, bool enable);
    mx_status_t (*get_int_status)(void* ctx, unsigned pin, bool* out_status);
    mx_status_t (*int_clear)(void* ctx, unsigned pin);
} gpio_protocol_ops_t;

typedef struct {
    gpio_protocol_ops_t* ops;
    void* ctx;
} gpio_protocol_t;

// configures a GPIO
static inline mx_status_t gpio_config(gpio_protocol_t* gpio, unsigned pin,
                                      gpio_config_flags_t flags) {
    return gpio->ops->config(gpio->ctx, pin, flags);
}

// reads the current value of a GPIO (0 or 1)
static inline mx_status_t gpio_read(gpio_protocol_t* gpio, unsigned pin, unsigned* out_value) {
    return gpio->ops->read(gpio->ctx, pin, out_value);
}

// sets the current value of the GPIO (any non-zero value maps to 1)
static inline mx_status_t gpio_write(gpio_protocol_t* gpio, unsigned pin, unsigned value) {
    return gpio->ops->write(gpio->ctx, pin, value);
}

// enables or disables interrupts for the  GPIO
static inline mx_status_t gpio_int_enable(gpio_protocol_t* gpio, unsigned pin, bool enable) {
    return gpio->ops->int_enable(gpio->ctx, pin, enable);
}

// returns whether an interrupt is pending for the GPIO
static inline mx_status_t gpio_get_int_status(gpio_protocol_t* gpio, unsigned pin,
                                              bool* out_active) {
    return gpio->ops->get_int_status(gpio->ctx, pin, out_active);
}

// clears the interrupt for a GPIO
static inline mx_status_t gpio_int_clear(gpio_protocol_t* gpio, unsigned pin) {
    return gpio->ops->int_clear(gpio->ctx, pin);
}

__END_CDECLS;
