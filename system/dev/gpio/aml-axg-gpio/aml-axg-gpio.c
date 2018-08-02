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
#define GPIO_INTERRUPT_POLARITY_SHIFT   16
#define MAX_GPIO_INDEX                  255
#define BITS_PER_GPIO_INTERRUPT         8

#define READ32_GPIO_REG(index, offset)              readl(io_buffer_virt(&gpio->mmios[index]) + offset*4)
#define WRITE32_GPIO_REG(index, offset, value)      writel(value, io_buffer_virt(&gpio->mmios[index]) + offset*4)

#define READ32_GPIO_INTERRUPT_REG(offset)           readl(io_buffer_virt(&gpio->mmio_interrupt) + offset*4)
#define WRITE32_GPIO_INTERRUPT_REG(offset, value)   writel(value, io_buffer_virt(&gpio->mmio_interrupt) + offset*4)


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
    uint32_t pull_offset;
    uint32_t pull_en_offset;
    uint32_t pin_start;
    mtx_t lock;
} aml_gpio_block_t;

typedef struct {
    uint32_t pin_0_3_select_offset;
    uint32_t pin_4_7_select_offset;
    uint32_t edge_polarity_offset;
    uint32_t filter_select_offset;
    uint32_t status_offset;
    uint32_t mask_offset;
    uint32_t irq_count;
    uint16_t *irq_info;
    mtx_t lock;
    uint8_t irq_status;
} aml_gpio_interrupt_t;

typedef struct {
    platform_device_protocol_t pdev;
    gpio_protocol_t gpio;
    zx_device_t* zxdev;
    io_buffer_t mmios[2];    // separate MMIO for AO domain
    io_buffer_t mmio_interrupt;
    aml_gpio_block_t* gpio_blocks;
    aml_gpio_interrupt_t* gpio_interrupt;
    size_t block_count;
} aml_gpio_t;

// MMIO indices (based on aml-gpio.c gpio_mmios)
enum {
    MMIO_GPIO               = 0,
    MMIO_GPIO_A0            = 1,
    MMIO_GPIO_INTERRUPTS    = 2,
};
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

    uint32_t pinid = index - block->pin_block;
    pinid += block->output_shift;
    mtx_lock(&block->lock);

    uint32_t regval = READ32_GPIO_REG(block->mmio_index, block->oen_offset);
    const uint32_t pinmask = 1 << pinid;
    uint32_t direction = flags & GPIO_DIR_MASK;
    if (direction & GPIO_DIR_OUT) {
        regval &= ~pinmask;
    } else {
        // Set the GPIO as pull-up or pull-down
        uint32_t pull = flags & GPIO_PULL_MASK;
        uint32_t pull_reg_val = READ32_GPIO_REG(block->mmio_index, block->pull_offset);
        uint32_t pull_en_reg_val = READ32_GPIO_REG(block->mmio_index, block->pull_en_offset);
        if (pull & GPIO_NO_PULL) {
            pull_en_reg_val &= ~(1 << pinmask);
        } else {
            if (pull & GPIO_PULL_UP) {
                pull_reg_val |= (1 << pinmask);
            } else {
                pull_reg_val &= ~(1 << pinmask);
            }
            pull_en_reg_val |= (1 << pinmask);
        }

        WRITE32_GPIO_REG(block->mmio_index, block->pull_offset, pull_reg_val);
        WRITE32_GPIO_REG(block->mmio_index, block->pull_en_offset, pull_en_reg_val);
        regval |= pinmask;
    }
    WRITE32_GPIO_REG(block->mmio_index, block->oen_offset, regval);

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

    // Sanity Check: pin_to_block must return a block that contains `pin`
    //               therefore `pin` must be greater than or equal to the first
    //               pin of the block.
    ZX_DEBUG_ASSERT(pin >= block->start_pin);

    // Each Pin Mux is controlled by a 4 bit wide field in `reg`
    // Compute the offset for this pin.
    uint32_t pin_shift = (pin - block->start_pin) * 4;
    pin_shift += block->output_shift;
    const uint32_t mux_mask = ~(0x0F << pin_shift);
    const uint32_t fn_val = fn << pin_shift;

    mtx_lock(&block->lock);

    uint32_t regval = READ32_GPIO_REG(block->mmio_index, block->mux_offset);
    regval &= mux_mask;     // Remove the previous value for the mux
    regval |= fn_val;       // Assign the new value to the mux
    WRITE32_GPIO_REG(block->mmio_index, block->mux_offset, regval);

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

    uint32_t pinindex = index - block->pin_block;
    pinindex += block->output_shift;
    const uint32_t readmask = 1 << pinindex;
    mtx_lock(&block->lock);

    const uint32_t regval = READ32_GPIO_REG(block->mmio_index, block->input_offset);

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
    pinindex += block->output_shift;

    mtx_lock(&block->lock);

    uint32_t regval = READ32_GPIO_REG(block->mmio_index, block->output_offset);
    if (value) {
        regval |= 1 << pinindex;
    } else {
        regval &= ~(1 << pinindex);
    }
    WRITE32_GPIO_REG(block->mmio_index, block->output_offset, regval);

    mtx_unlock(&block->lock);

    return ZX_OK;
}


static uint32_t aml_gpio_get_unsed_irq_index(uint8_t status) {
    // First isolate the rightmost 0-bit
    uint8_t zero_bit_set = ~status & (status+1);
    // Count no. of leading zeros
    return __builtin_ctz(zero_bit_set);
}

static zx_status_t aml_gpio_get_interrupt(void *ctx, uint32_t pin,
                                          uint32_t flags,
                                          zx_handle_t *out_handle) {
    aml_gpio_t* gpio = ctx;
    zx_status_t status = ZX_OK;
    aml_gpio_interrupt_t* interrupt = gpio->gpio_interrupt;

    if (pin > MAX_GPIO_INDEX) {
        return ZX_ERR_INVALID_ARGS;
    }
    mtx_lock(&interrupt->lock);

    uint32_t index = aml_gpio_get_unsed_irq_index(interrupt->irq_status);
    if (index > interrupt->irq_count) {
        status = ZX_ERR_NO_RESOURCES;
        goto fail;
    }

    for (uint32_t i=0; i<interrupt->irq_count; i++) {
        if(interrupt->irq_info[i] == pin) {
            zxlogf(ERROR, "GPIO Interrupt already configured for this pin %u\n", (int)index);
            status = ZX_ERR_ALREADY_EXISTS;
            goto fail;
        }
    }
    zxlogf(INFO, "GPIO Interrupt index %d allocated\n", (int)index);
    aml_gpio_block_t* block;
    if ((status = aml_pin_to_block(gpio, pin, &block)) != ZX_OK) {
        zxlogf(ERROR, "aml_gpio_read: pin not found %u\n", pin);
        goto fail;
    }
    uint32_t flags_ = flags;
    if (flags == ZX_INTERRUPT_MODE_EDGE_LOW) {
        // GPIO controller sets the polarity
        flags_ = ZX_INTERRUPT_MODE_EDGE_HIGH;
    } else if (flags == ZX_INTERRUPT_MODE_LEVEL_LOW) {
        flags_ = ZX_INTERRUPT_MODE_LEVEL_HIGH;
    }

    // Create Interrupt Object
    status = pdev_get_interrupt(&gpio->pdev, index, flags_,
                                    out_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_gpio_get_interrupt: pdev_map_interrupt failed %d\n", status);
        goto fail;
    }

    // Configure GPIO interrupt
    uint32_t pin_select_offset  = ((index>3)? interrupt->pin_4_7_select_offset: interrupt->pin_0_3_select_offset);
    // Select GPIO IRQ(index) and program it to
    // the requested GPIO PIN
    uint32_t regval = READ32_GPIO_INTERRUPT_REG(pin_select_offset);
    regval |= (((pin - block->pin_block) + block->pin_start) << (index * BITS_PER_GPIO_INTERRUPT));
    WRITE32_GPIO_INTERRUPT_REG(pin_select_offset, regval);
    // Configure GPIO Interrupt EDGE and Polarity
    uint32_t mode_reg_val = READ32_GPIO_INTERRUPT_REG(interrupt->edge_polarity_offset);

    switch (flags & ZX_INTERRUPT_MODE_MASK) {
    case ZX_INTERRUPT_MODE_EDGE_LOW:
        mode_reg_val = mode_reg_val | (1 << index);
        mode_reg_val = mode_reg_val | ((1 << index) << GPIO_INTERRUPT_POLARITY_SHIFT);
        break;
    case ZX_INTERRUPT_MODE_EDGE_HIGH:
        mode_reg_val = mode_reg_val | (1 << index);
        mode_reg_val = mode_reg_val & ~((1 << index) << GPIO_INTERRUPT_POLARITY_SHIFT);
        break;
    case ZX_INTERRUPT_MODE_LEVEL_LOW:
        mode_reg_val = mode_reg_val & ~(1 << index);
        mode_reg_val = mode_reg_val | ((1 << index) << GPIO_INTERRUPT_POLARITY_SHIFT);
        break;
    case ZX_INTERRUPT_MODE_LEVEL_HIGH:
        mode_reg_val = mode_reg_val & ~(1 << index);
        mode_reg_val = mode_reg_val & ~((1 << index) << GPIO_INTERRUPT_POLARITY_SHIFT);
        break;
    default:
        status = ZX_ERR_INVALID_ARGS;
        goto fail;
    }
    WRITE32_GPIO_INTERRUPT_REG(interrupt->edge_polarity_offset, mode_reg_val);

    // Configure Interrupt Select Filter
    regval = READ32_GPIO_INTERRUPT_REG(interrupt->filter_select_offset);
    WRITE32_GPIO_INTERRUPT_REG(interrupt->filter_select_offset, regval | (0x7 << index));
    interrupt->irq_status |= 1 << index;
    interrupt->irq_info[index] = pin;
fail:
    mtx_unlock(&interrupt->lock);
    return status;
}

static zx_status_t aml_gpio_release_interrupt(void *ctx, uint32_t pin) {
    aml_gpio_t* gpio = ctx;
    aml_gpio_interrupt_t* interrupt = gpio->gpio_interrupt;
    mtx_lock(&interrupt->lock);
    for (uint32_t i=0; i<interrupt->irq_count; i++) {
        if(interrupt->irq_info[i] == pin) {
            interrupt->irq_status &= ~(1 << i);
            interrupt->irq_info[i] = MAX_GPIO_INDEX+1;
            goto fail;
        }
    }
fail:
    mtx_unlock(&interrupt->lock);
    return ZX_ERR_NOT_FOUND;
}

static zx_status_t aml_gpio_set_polarity(void *ctx, uint32_t pin,
                                        uint32_t polarity) {
    aml_gpio_t* gpio = ctx;
    aml_gpio_interrupt_t* interrupt = gpio->gpio_interrupt;
    int irq_index = -1;
    if (pin > MAX_GPIO_INDEX) {
        return ZX_ERR_INVALID_ARGS;
    }

   for (uint32_t i=0; i<interrupt->irq_count; i++) {
        if(interrupt->irq_info[i] == pin) {
            irq_index = i;
            break;
        }
    }
    if (irq_index == -1) {
        return ZX_ERR_NOT_FOUND;
    }

    mtx_lock(&interrupt->lock);
    // Configure GPIO Interrupt EDGE and Polarity
    uint32_t mode_reg_val = READ32_GPIO_INTERRUPT_REG(interrupt->edge_polarity_offset);
    if (polarity) {
        mode_reg_val &= ~((1 << irq_index) << GPIO_INTERRUPT_POLARITY_SHIFT);
    } else {
        mode_reg_val |= ((1 << irq_index) << GPIO_INTERRUPT_POLARITY_SHIFT);
    }
    WRITE32_GPIO_INTERRUPT_REG(interrupt->edge_polarity_offset, mode_reg_val);
    mtx_unlock(&interrupt->lock);
    return ZX_OK;
}

static gpio_protocol_ops_t gpio_ops = {
    .config = aml_gpio_config,
    .set_alt_function = aml_gpio_set_alt_function,
    .read = aml_gpio_read,
    .write = aml_gpio_write,
    .get_interrupt = aml_gpio_get_interrupt,
    .release_interrupt = aml_gpio_release_interrupt,
    .set_polarity = aml_gpio_set_polarity,
};

static void aml_gpio_release(void* ctx) {
    aml_gpio_t* gpio = ctx;
    io_buffer_release(&gpio->mmios[MMIO_GPIO]);
    io_buffer_release(&gpio->mmios[MMIO_GPIO_A0]);
    io_buffer_release(&gpio->mmio_interrupt);
    free(gpio->gpio_interrupt->irq_info);
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

    status = pdev_map_mmio_buffer(&gpio->pdev, MMIO_GPIO, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                    &gpio->mmios[MMIO_GPIO]);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_gpio_bind: pdev_map_mmio_buffer failed\n");
        goto fail;
    }

    status = pdev_map_mmio_buffer(&gpio->pdev, MMIO_GPIO_A0, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                    &gpio->mmios[MMIO_GPIO_A0]);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_gpio_bind: pdev_map_mmio_buffer failed\n");
        goto fail;
    }

    status = pdev_map_mmio_buffer(&gpio->pdev, MMIO_GPIO_INTERRUPTS, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                    &gpio->mmio_interrupt);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_gpio_bind: pdev_map_mmio_buffer failed\n");
        goto fail;
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
        gpio->gpio_interrupt = &a113_interrupt_block;
        break;
    case PDEV_PID_AMLOGIC_S905D2:
        gpio->gpio_blocks = s905d2_gpio_blocks;
        gpio->block_count = countof(s905d2_gpio_blocks);
        gpio->gpio_interrupt = &s905d2_interrupt_block;
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

    gpio->gpio_interrupt->irq_count = info.irq_count;
    gpio->gpio_interrupt->irq_status = 0;
    gpio->gpio.ops = &gpio_ops;
    gpio->gpio.ctx = gpio;
    pbus_set_protocol(&pbus, ZX_PROTOCOL_GPIO, &gpio->gpio);
    gpio->gpio_interrupt->irq_info = calloc(gpio->gpio_interrupt->irq_count,
                                     sizeof(uint8_t));
    if (!gpio->gpio_interrupt->irq_info) {
        zxlogf(ERROR, "aml_gpio_bind: irq_info calloc failed\n");
        goto fail;
    }

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
