// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <threads.h>

#include <bits/limits.h>
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <hw/reg.h>

#include <zircon/assert.h>
#include <zircon/types.h>

#define PINS_PER_BLOCK  32
#define ALT_FUNCTION_MAX 5

typedef struct {
    uint32_t pin_count;
    uint32_t oen_offset;
    uint32_t input_offset;
    uint32_t output_offset;
    uint32_t output_shift;  // Used for GPIOAO block
    uint32_t mmio_index;
    mtx_t lock;
} aml_gpio_block_t;

typedef struct {
    // pinmux register offsets for the alternate functions.
    // zero means alternate function not supported.
    uint8_t regs[ALT_FUNCTION_MAX];
    // bit number to set/clear to enable/disable alternate function
    uint8_t bits[ALT_FUNCTION_MAX];
} aml_pinmux_t;

typedef struct {
    aml_pinmux_t mux[PINS_PER_BLOCK];
} aml_pinmux_block_t;

typedef struct {
    platform_device_protocol_t pdev;
    gpio_protocol_t gpio;
    zx_device_t* zxdev;
    pdev_vmo_buffer_t mmios[2];    // separate MMIO for AO domain
    aml_gpio_block_t* gpio_blocks;
    const aml_pinmux_block_t* pinmux_blocks;
    size_t block_count;
    mtx_t pinmux_lock;
} aml_gpio_t;

#include "s912-blocks.h"
#include "s905x-blocks.h"

static zx_status_t aml_pin_to_block(aml_gpio_t* gpio, const uint32_t pin,
                                    aml_gpio_block_t** out_block, uint32_t* out_pin_index) {
    ZX_DEBUG_ASSERT(out_block && out_pin_index);

    uint32_t block_index = pin / PINS_PER_BLOCK;
    if (block_index >= gpio->block_count) {
        return ZX_ERR_NOT_FOUND;
    }
    aml_gpio_block_t* block = &gpio->gpio_blocks[block_index];
    uint32_t pin_index = pin % PINS_PER_BLOCK;
    if (pin_index >= block->pin_count) {
        return ZX_ERR_NOT_FOUND;
    }

    *out_block = block;
    *out_pin_index = pin_index;
    return ZX_OK;
}

static zx_status_t aml_gpio_config(void* ctx, uint32_t index, uint32_t flags) {
    aml_gpio_t* gpio = ctx;
    zx_status_t status;

    aml_gpio_block_t* block;
    uint32_t pin_index;
    if ((status = aml_pin_to_block(gpio, index, &block, &pin_index)) != ZX_OK) {
        zxlogf(ERROR, "aml_gpio_config: pin not found %u\n", index);
        return status;
    }

    volatile uint32_t* reg = (volatile uint32_t *)gpio->mmios[block->mmio_index].vaddr;
    reg += block->oen_offset;

    mtx_lock(&block->lock);

    uint32_t regval = readl(reg);

    if (flags & GPIO_DIR_OUT) {
        regval &= ~(1 << pin_index);
    } else {
        regval |= (1 << pin_index);
    }

    writel(regval, reg);

    mtx_unlock(&block->lock);

    return ZX_OK;
}

// Configure a pin for an alternate function specified by function
static zx_status_t aml_gpio_set_alt_function(void* ctx, const uint32_t pin, uint32_t function) {
    aml_gpio_t* gpio = ctx;

    if (function > ALT_FUNCTION_MAX) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    uint32_t block_index = pin / PINS_PER_BLOCK;
    if (block_index >= gpio->block_count) {
        return ZX_ERR_NOT_FOUND;
    }
    const aml_pinmux_block_t* block = &gpio->pinmux_blocks[block_index];
    uint32_t pin_index = pin % PINS_PER_BLOCK;
    const aml_pinmux_t* mux = &block->mux[pin_index];

    aml_gpio_block_t* gpio_block = &gpio->gpio_blocks[block_index];
    volatile uint32_t* reg = (volatile uint32_t *)gpio->mmios[gpio_block->mmio_index].vaddr;

    mtx_lock(&gpio->pinmux_lock);

    for (uint i = 0; i < ALT_FUNCTION_MAX; i++) {
        uint32_t reg_index = mux->regs[i];

        if (reg_index) {
            uint32_t mask = (1 << mux->bits[i]);
            uint32_t regval = readl(reg + reg_index);

            if (i == function - 1) {
                regval |= mask;
            } else {
                regval &= ~mask;
            }

            writel(regval, reg + reg_index);
        }
    }

    mtx_unlock(&gpio->pinmux_lock);

    return ZX_OK;
}

static zx_status_t aml_gpio_read(void* ctx, uint32_t pin, uint8_t* out_value) {
    aml_gpio_t* gpio = ctx;
    zx_status_t status;

    aml_gpio_block_t* block;
    uint32_t pin_index;
    if ((status = aml_pin_to_block(gpio, pin, &block, &pin_index)) != ZX_OK) {
        zxlogf(ERROR, "aml_gpio_read: pin not found %u\n", pin);
        return status;
    }

    const uint32_t readmask = 1 << pin_index;

    volatile uint32_t* reg = (volatile uint32_t *)gpio->mmios[block->mmio_index].vaddr;
    reg += block->input_offset;

    mtx_lock(&block->lock);

    const uint32_t regval = readl(reg);

    mtx_unlock(&block->lock);

    if (regval & readmask) {
        *out_value = 1;
    } else {
        *out_value = 0;
    }

    return ZX_OK;
}

static zx_status_t aml_gpio_write(void* ctx, uint32_t pin, uint8_t value) {
    aml_gpio_t* gpio = ctx;
    zx_status_t status;

    aml_gpio_block_t* block;
    uint32_t pin_index;
    if ((status = aml_pin_to_block(gpio, pin, &block, &pin_index)) != ZX_OK) {
        zxlogf(ERROR, "aml_gpio_write: pin not found %u\n", pin);
        return status;
    }

    volatile uint32_t* reg = (volatile uint32_t *)gpio->mmios[block->mmio_index].vaddr;
    reg += block->output_offset;
    pin_index += block->output_shift;

    mtx_lock(&block->lock);

    uint32_t regval = readl(reg);

    if (value) {
        regval |= (1 << pin_index);
    } else {
        regval &= ~(1 << pin_index);
    }

    writel(regval, reg);

    mtx_unlock(&block->lock);

    return ZX_OK;
}

static gpio_protocol_ops_t gpio_ops = {
    .config = aml_gpio_config,
    .set_alt_function = aml_gpio_set_alt_function,
    .read = aml_gpio_read,
    .write = aml_gpio_write,
};

static void aml_gpio_release(void* ctx) {
    aml_gpio_t* gpio = ctx;
    for (unsigned i = 0; i < countof(gpio->mmios); i++) {
        pdev_vmo_buffer_release(&gpio->mmios[i]);
    }
    free(gpio);
}


static zx_protocol_device_t gpio_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = aml_gpio_release,
};

static zx_status_t aml_gpio_bind(void* ctx, zx_device_t* parent) {
    zx_status_t status;

    aml_gpio_t* gpio = calloc(1, sizeof(aml_gpio_t));
    if (!gpio) {
        return ZX_ERR_NO_MEMORY;
    }
    mtx_init(&gpio->pinmux_lock, mtx_plain);

    if ((status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &gpio->pdev)) != ZX_OK) {
        zxlogf(ERROR, "aml_gpio_bind: ZX_PROTOCOL_PLATFORM_DEV not available\n");
        goto fail;
    }

    platform_bus_protocol_t pbus;
    if ((status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_BUS, &pbus)) != ZX_OK) {
        zxlogf(ERROR, "aml_gpio_bind: ZX_PROTOCOL_PLATFORM_BUS not available\n");
        goto fail;
    }

    for (unsigned i = 0; i < countof(gpio->mmios); i++) {
        status = pdev_map_mmio_buffer(&gpio->pdev, i, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                      &gpio->mmios[i]);
        if (status != ZX_OK) {
            zxlogf(ERROR, "aml_gpio_bind: pdev_map_mmio_buffer failed\n");
            goto fail;
        }
    }

    pdev_device_info_t info;
    status = pdev_get_device_info(&gpio->pdev, &info);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_gpio_bind: pdev_get_device_info failed\n");
        goto fail;
    }

    switch (info.pid) {
    case PDEV_PID_AMLOGIC_S912:
        gpio->gpio_blocks = s912_gpio_blocks;
        gpio->pinmux_blocks = s912_pinmux_blocks;
        gpio->block_count = countof(s912_gpio_blocks);
        break;
    case PDEV_PID_AMLOGIC_S905X:
        gpio->gpio_blocks = s905x_gpio_blocks;
        gpio->pinmux_blocks = s905x_pinmux_blocks;
        gpio->block_count = countof(s905x_gpio_blocks);
        break;
    default:
        zxlogf(ERROR, "aml_gpio_bind: unsupported SOC PID %u\n", info.pid);
        goto fail;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "aml-gpio",
        .ctx = gpio,
        .ops = &gpio_device_proto,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    status = device_add(parent, &args, &gpio->zxdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_gpio_bind: device_add failed\n");
        goto fail;
    }

    gpio->gpio.ops = &gpio_ops;
    gpio->gpio.ctx = gpio;
    pbus_set_protocol(&pbus, ZX_PROTOCOL_GPIO, &gpio->gpio);

    return ZX_OK;

fail:
    aml_gpio_release(gpio);
    return status;
}

static zx_driver_ops_t aml_gpio_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = aml_gpio_bind,
};

ZIRCON_DRIVER_BEGIN(aml_gpio, aml_gpio_driver_ops, "zircon", "0.1", 5)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_GPIO),
    // we support multiple SOC variants
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S912),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S905X),
ZIRCON_DRIVER_END(aml_gpio)
