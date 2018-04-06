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

#define ALT_FUNCTION_MAX 15

typedef struct {
    uint32_t start_pin;
    uint32_t pin_block;
    uint32_t pin_count;
    uint32_t mux_offset;
    uint32_t oen_offset;
    uint32_t input_offset;
    uint32_t output_offset;
    uint32_t output_shift;  // Used for GPIOAO block
    uint32_t mmio_index;
    mtx_t lock;
} aml_gpio_block_t;

typedef struct {
    platform_device_protocol_t pdev;
    gpio_protocol_t gpio;
    zx_device_t* zxdev;
    io_buffer_t mmios[2];    // separate MMIO for AO domain
    aml_gpio_block_t* gpio_blocks;
    size_t block_count;
} aml_gpio_t;

#include "a113-blocks.h"
#include "s905d2-blocks.h"

static zx_status_t aml_pin_to_block(aml_gpio_t* gpio, const uint32_t pinid, aml_gpio_block_t** result) {
    ZX_DEBUG_ASSERT(result);

    for (size_t i = 0; i < gpio->block_count; i++) {
        aml_gpio_block_t* gpio_block = &gpio->gpio_blocks[i];
        const uint32_t end_pin = gpio_block->start_pin + gpio_block->pin_count;
        if (pinid >= gpio_block->start_pin && pinid < end_pin) {
            *result = gpio_block;
            return ZX_OK;
        }
    }

    return ZX_ERR_NOT_FOUND;
}

static zx_status_t aml_gpio_config(void* ctx, uint32_t index, uint32_t flags) {
    aml_gpio_t* gpio = ctx;
    zx_status_t status;

    aml_gpio_block_t* block;
    if ((status = aml_pin_to_block(gpio, index, &block)) != ZX_OK) {
        zxlogf(ERROR, "aml_gpio_config: pin not found %u\n", index);
        return status;
    }

    const uint32_t pinid = index - block->pin_block;

    mtx_lock(&block->lock);

    volatile uint32_t* reg = (volatile uint32_t*)io_buffer_virt(&gpio->mmios[block->mmio_index]);
    reg += block->oen_offset;
    uint32_t regval = readl(reg);
    const uint32_t pinmask = 1 << pinid;

    if (flags & GPIO_DIR_OUT) {
        regval &= ~pinmask;
    } else {
        regval |= pinmask;
    }

    writel(regval, reg);

    mtx_unlock(&block->lock);

    return ZX_OK;
}

// Configure a pin for an alternate function specified by fn
static zx_status_t aml_gpio_set_alt_function(void* ctx, const uint32_t pin, const uint64_t fn) {
    aml_gpio_t* gpio = ctx;

    if (fn > ALT_FUNCTION_MAX) {
        zxlogf(ERROR, "aml_config_pinmux: pin mux alt config out of range"
                " %lu\n", fn);
        return ZX_ERR_OUT_OF_RANGE;
    }

    zx_status_t status;

    aml_gpio_block_t* block;
    if (((status = aml_pin_to_block(gpio, pin, &block)) != ZX_OK) != ZX_OK) {
        zxlogf(ERROR, "aml_config_pinmux: pin not found %u\n", pin);
        return status;
    }

    // Points to the control register.
    volatile uint32_t* reg = (volatile uint32_t*)io_buffer_virt(&gpio->mmios[block->mmio_index]);
    reg += block->mux_offset;

    // Sanity Check: pin_to_block must return a block that contains `pin`
    //               therefore `pin` must be greater than or equal to the first
    //               pin of the block.
    ZX_DEBUG_ASSERT(pin >= block->start_pin);

    // Each Pin Mux is controlled by a 4 bit wide field in `reg`
    // Compute the offset for this pin.
    const uint32_t pin_shift = (pin - block->start_pin) * 4;
    const uint32_t mux_mask = ~(0x0F << pin_shift);
    const uint32_t fn_val = fn << pin_shift;

    mtx_lock(&block->lock);

    uint32_t regval = readl(reg);
    regval &= mux_mask;     // Remove the previous value for the mux
    regval |= fn_val;       // Assign the new value to the mux
    writel(regval, reg);

    mtx_unlock(&block->lock);

    return ZX_OK;
}

static zx_status_t aml_gpio_read(void* ctx, uint32_t index, uint8_t* out_value) {
    aml_gpio_t* gpio = ctx;
    zx_status_t status;

    aml_gpio_block_t* block;
    if ((status = aml_pin_to_block(gpio, index, &block)) != ZX_OK) {
        zxlogf(ERROR, "aml_config_pinmux: pin not found %u\n", index);
        return status;
    }

    const uint32_t pinindex = index - block->pin_block;
    const uint32_t readmask = 1 << pinindex;

    volatile uint32_t* reg = (volatile uint32_t*)io_buffer_virt(&gpio->mmios[block->mmio_index]);
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

static zx_status_t aml_gpio_write(void* ctx, uint32_t index, uint8_t value) {
    aml_gpio_t* gpio = ctx;
    zx_status_t status;

    aml_gpio_block_t* block;
    if ((status = aml_pin_to_block(gpio, index, &block)) != ZX_OK) {
        zxlogf(ERROR, "aml_gpio_write: pin not found %u\n", index);
        return status;
    }

    uint32_t pinindex = index - block->pin_block;

    volatile uint32_t* reg = (volatile uint32_t*)io_buffer_virt(&gpio->mmios[block->mmio_index]);
    reg += block->output_offset;
    pinindex += block->output_shift;

    mtx_lock(&block->lock);

    uint32_t regval = readl(reg);

    if (value) {
        regval |= 1 << pinindex;
    } else {
        regval &= ~(1 << pinindex);
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
        io_buffer_release(&gpio->mmios[i]);
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
    case PDEV_PID_AMLOGIC_A113:
        gpio->gpio_blocks = a113_gpio_blocks;
        gpio->block_count = countof(a113_gpio_blocks);
        break;
    case PDEV_PID_AMLOGIC_S905D2:
        gpio->gpio_blocks = s905d2_gpio_blocks;
        gpio->block_count = countof(s905d2_gpio_blocks);
        break;
    default:
        zxlogf(ERROR, "aml_gpio_bind: unsupported SOC PID %u\n", info.pid);
        goto fail;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "aml-axg-gpio",
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
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_A113),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S905D2),
ZIRCON_DRIVER_END(aml_gpio)
