// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <gpio/pl061/pl061.h>
#include <soc/hi3660/hi3660.h>

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

// TODO(voydanoff) Move hard coded values to a header file

// Addresses for GPIO regions
#define GPIO_0_ADDR     0xe8a0b000
#define GPIO_18_ADDR    0xff3b4000
#define GPIO_20_ADDR    0xe8a1f000
#define GPIO_22_ADDR    0xfff0b000
#define GPIO_28_ADDR    0xfff1d000

static pl061_gpios_t* find_gpio(hi3660_t* hi3660, uint32_t index) {
    pl061_gpios_t* gpios;
    // TODO(voydanoff) consider using a fancier data structure here
    list_for_every_entry(&hi3660->gpios, gpios, pl061_gpios_t, node) {
        if (index >= gpios->gpio_start && index < gpios->gpio_start + gpios->gpio_count) {
            return gpios;
        }
    }
    zxlogf(ERROR, "find_gpio failed for index %u\n", index);
    return NULL;
}

static zx_status_t hi3660_gpio_config(void* ctx, uint32_t index, uint32_t flags) {
    hi3660_t* hi3660 = ctx;
    pl061_gpios_t* gpios = find_gpio(hi3660, index);
    if (!gpios) {
        return ZX_ERR_INVALID_ARGS;
    }
    return pl061_proto_ops.config(gpios, index, flags);
}

static zx_status_t hi3660_gpio_read(void* ctx, uint32_t index, uint8_t* out_value) {
    hi3660_t* hi3660 = ctx;
    pl061_gpios_t* gpios = find_gpio(hi3660, index);
    if (!gpios) {
        return ZX_ERR_INVALID_ARGS;
    }
    return pl061_proto_ops.read(gpios, index, out_value);
}

static zx_status_t hi3660_gpio_write(void* ctx, uint32_t index, uint8_t value) {
    hi3660_t* hi3660 = ctx;
    pl061_gpios_t* gpios = find_gpio(hi3660, index);
    if (!gpios) {
        return ZX_ERR_INVALID_ARGS;
    }
    return pl061_proto_ops.write(gpios, index, value);
}

static gpio_protocol_ops_t gpio_ops = {
    .config = hi3660_gpio_config,
    .read = hi3660_gpio_read,
    .write = hi3660_gpio_write,
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

zx_status_t hi3660_gpio_init(hi3660_t* hi3660, zx_handle_t bti) {
    zx_status_t status;
    zx_handle_t resource = get_root_resource();

    for (size_t i = 0; i < countof(gpio_blocks); i++) {
        const gpio_block_t* block = &gpio_blocks[i];

        pl061_gpios_t* gpios = calloc(1, sizeof(pl061_gpios_t));
        if (!gpios) {
            return ZX_ERR_NO_MEMORY;
        }

        status = io_buffer_init_physical(&gpios->buffer, bti, block->base, block->length,
                                         resource, ZX_CACHE_POLICY_UNCACHED_DEVICE);
        if (status != ZX_OK) {
            zxlogf(ERROR, "hi3660_gpio_init: io_buffer_init_physical failed %d\n", status);
            free(gpios);
            return status;
        }

        mtx_init(&gpios->lock, mtx_plain);
        gpios->gpio_start = block->start_pin;
        gpios->gpio_count = block->pin_count;
        gpios->irqs = block->irqs;
        gpios->irq_count = block->irq_count;
        list_add_tail(&hi3660->gpios, &gpios->node);
    }

    hi3660->gpio.ops = &gpio_ops;
    hi3660->gpio.ctx = hi3660;

    return ZX_OK;
}

void hi3660_gpio_release(hi3660_t* hi3660) {
    pl061_gpios_t* gpios;

    while ((gpios = list_remove_head_type(&hi3660->gpios, pl061_gpios_t, node)) != NULL) {
        io_buffer_release(&gpios->buffer);
        free(gpios);
    }
}
