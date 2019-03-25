// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <gpio/pl061/pl061.h>

#include <stdlib.h>

#include "hikey960.h"

// TODO(voydanoff) Move hard coded values to a header file

// Addresses for GPIO regions
#define GPIO_0_ADDR     0xe8a0b000
#define GPIO_18_ADDR    0xff3b4000
#define GPIO_20_ADDR    0xe8a1f000
#define GPIO_22_ADDR    0xfff0b000
#define GPIO_28_ADDR    0xfff1d000

static pl061_gpios_t* find_gpio(hikey960_t* hikey, uint32_t index) {
    pl061_gpios_t* gpios;
    // TODO(voydanoff) consider using a fancier data structure here
    list_for_every_entry(&hikey->gpios, gpios, pl061_gpios_t, node) {
        if (index >= gpios->gpio_start && index < gpios->gpio_start + gpios->gpio_count) {
            return gpios;
        }
    }
    zxlogf(ERROR, "find_gpio failed for index %u\n", index);
    return NULL;
}

static zx_status_t hikey960_gpio_config_in(void* ctx, uint32_t index, uint32_t flags) {
    hikey960_t* hikey = ctx;
    pl061_gpios_t* gpios = find_gpio(hikey, index);
    if (!gpios) {
        return ZX_ERR_INVALID_ARGS;
    }
    return pl061_proto_ops.config_in(gpios, index, flags);
}

static zx_status_t hikey960_gpio_config_out(void* ctx, uint32_t index, uint8_t initial_value) {
    hikey960_t* hikey = ctx;
    pl061_gpios_t* gpios = find_gpio(hikey, index);
    if (!gpios) {
        return ZX_ERR_INVALID_ARGS;
    }
    return pl061_proto_ops.config_out(gpios, index, initial_value);
}

static zx_status_t hikey960_gpio_set_alt_function(void* ctx, uint32_t index, uint64_t function) {
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t hikey960_gpio_read(void* ctx, uint32_t index, uint8_t* out_value) {
    hikey960_t* hikey = ctx;
    pl061_gpios_t* gpios = find_gpio(hikey, index);
    if (!gpios) {
        return ZX_ERR_INVALID_ARGS;
    }
    return pl061_proto_ops.read(gpios, index, out_value);
}

static zx_status_t hikey960_gpio_write(void* ctx, uint32_t index, uint8_t value) {
    hikey960_t* hikey = ctx;
    pl061_gpios_t* gpios = find_gpio(hikey, index);
    if (!gpios) {
        return ZX_ERR_INVALID_ARGS;
    }
    return pl061_proto_ops.write(gpios, index, value);
}

static zx_status_t hikey960_gpio_get_interrupt(void* ctx, uint32_t pin, uint32_t flags,
                                             zx_handle_t* out_handle) {
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t hikey960_gpio_release_interrupt(void* ctx, uint32_t pin) {
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t hikey960_gpio_set_polarity(void* ctx, uint32_t pin, uint32_t polarity) {
    return ZX_ERR_NOT_SUPPORTED;
}

static gpio_impl_protocol_ops_t gpio_ops = {
    .config_in = hikey960_gpio_config_in,
    .config_out = hikey960_gpio_config_out,
    .set_alt_function = hikey960_gpio_set_alt_function,
    .read = hikey960_gpio_read,
    .write = hikey960_gpio_write,
    .get_interrupt = hikey960_gpio_get_interrupt,
    .release_interrupt = hikey960_gpio_release_interrupt,
    .set_polarity = hikey960_gpio_set_polarity,
};

typedef struct {
    zx_paddr_t  base;
    size_t      length;
    uint32_t    start_pin;
    uint32_t    pin_count;
    const uint32_t* irqs;
    uint32_t    irq_count;
} gpio_block_t;

static const uint32_t irqs_0[] = {
    116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133,
};

static const uint32_t irqs_18[] = {
    134, 135,
};

static const uint32_t irqs_20[] = {
    136, 137,
};

static const uint32_t irqs_22[] = {
    138, 139, 140, 141, 142, 143,
};

static const uint32_t irqs_28[] = {
    173,
};

static const gpio_block_t gpio_blocks[] = {
    {
        // GPIO groups 0 - 17
        .base = GPIO_0_ADDR,
        .length = 18 * 4096,
        .start_pin = 0,
        .pin_count = 18 * 8,
        .irqs = irqs_0,
        .irq_count = countof(irqs_0),
    },
    {
        // GPIO groups 18 and 19
        .base = GPIO_18_ADDR,
        .length = 2 * 4096,
        .start_pin = 18 * 8,
        .pin_count = 2 * 8,
        .irqs = irqs_18,
        .irq_count = countof(irqs_18),
    },
    {
        // GPIO groups 20 and 21
        .base = GPIO_20_ADDR,
        .length = 2 * 4096,
        .start_pin = 20 * 8,
        .pin_count = 2 * 8,
        .irqs = irqs_20,
        .irq_count = countof(irqs_20),
    },
    {
        // GPIO groups 22 - 27
        .base = GPIO_22_ADDR,
        .length = 6 * 4096,
        .start_pin = 22 * 8,
        .pin_count = 6 * 8,
        .irqs = irqs_22,
        .irq_count = countof(irqs_22),
    },
    {
        // GPIO group 28
        .base = GPIO_28_ADDR,
        .length = 1 * 4096,
        .start_pin = 28 * 8,
        .pin_count = 1 * 8,
        .irqs = irqs_28,
        .irq_count = countof(irqs_28),
    },
};

zx_status_t hikey960_gpio_init(hikey960_t* hikey) {
    zx_status_t status;
    zx_handle_t resource = get_root_resource();

    for (size_t i = 0; i < countof(gpio_blocks); i++) {
        const gpio_block_t* block = &gpio_blocks[i];

        pl061_gpios_t* gpios = calloc(1, sizeof(pl061_gpios_t));
        if (!gpios) {
            return ZX_ERR_NO_MEMORY;
        }

        status = mmio_buffer_init_physical(&gpios->buffer, block->base, block->length,
                                           resource, ZX_CACHE_POLICY_UNCACHED_DEVICE);
        if (status != ZX_OK) {
            zxlogf(ERROR, "hikey960_gpio_init: mmio_buffer_init_physical failed %d\n", status);
            free(gpios);
            return status;
        }

        mtx_init(&gpios->lock, mtx_plain);
        gpios->gpio_start = block->start_pin;
        gpios->gpio_count = block->pin_count;
        gpios->irqs = block->irqs;
        gpios->irq_count = block->irq_count;
        list_add_tail(&hikey->gpios, &gpios->node);
    }

    hikey->gpio.ops = &gpio_ops;
    hikey->gpio.ctx = hikey;

    return ZX_OK;
}

void hikey960_gpio_release(hikey960_t* hikey) {
    pl061_gpios_t* gpios;

    while ((gpios = list_remove_head_type(&hikey->gpios, pl061_gpios_t, node)) != NULL) {
        mmio_buffer_release(&gpios->buffer);
        free(gpios);
    }
}
