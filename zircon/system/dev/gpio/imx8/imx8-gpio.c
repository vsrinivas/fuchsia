// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "imx8-gpio.h"

#include <bits/limits.h>
#include <ddk/debug.h>
#include <hw/reg.h>
#include <soc/imx8m/imx8m-iomux.h>
#include <zircon/assert.h>
#include <zircon/types.h>
#include <zircon/syscalls/port.h>

zx_status_t imx8_gpio_config_in(void* ctx, uint32_t pin, uint32_t flags) {
    uint32_t gpio_block;
    uint32_t gpio_pin;
    uint32_t regVal;
    imx8_gpio_t* gpio = ctx;

    gpio_block = IMX_NUM_TO_BLOCK(pin);
    gpio_pin = IMX_NUM_TO_BIT(pin);

    if (gpio_block >= IMX_GPIO_BLOCKS || gpio_pin >= 32) {
        zxlogf(ERROR, "%s: Invalid GPIO pin (pin = %d Block = %d, Offset = %d)\n",
               __FUNCTION__, pin, gpio_block, gpio_pin);
        return ZX_ERR_INVALID_ARGS;
    }

    mtx_lock(&gpio->lock[gpio_block]);
    regVal = READ32_GPIO_REG(gpio_block, IMX_GPIO_GDIR);
    regVal &= ~(1 << gpio_pin);
    regVal |= (GPIO_INPUT << gpio_pin);
    WRITE32_GPIO_REG(gpio_block, IMX_GPIO_GDIR, regVal);
    mtx_unlock(&gpio->lock[gpio_block]);
    return ZX_OK;
}

zx_status_t imx8_gpio_config_out(void* ctx, uint32_t pin, uint8_t initial_value) {
    uint32_t gpio_block;
    uint32_t gpio_pin;
    uint32_t regVal;
    imx8_gpio_t* gpio = ctx;

    gpio_block = IMX_NUM_TO_BLOCK(pin);
    gpio_pin = IMX_NUM_TO_BIT(pin);

    if (gpio_block >= IMX_GPIO_BLOCKS || gpio_pin >= 32) {
        zxlogf(ERROR, "%s: Invalid GPIO pin (pin = %d Block = %d, Offset = %d)\n",
               __FUNCTION__, pin, gpio_block, gpio_pin);
        return ZX_ERR_INVALID_ARGS;
    }

    mtx_lock(&gpio->lock[gpio_block]);

    // Set value before configuring for output
    regVal = READ32_GPIO_REG(gpio_block, IMX_GPIO_DR);
    regVal &= ~(1 << gpio_pin);
    regVal |= (initial_value << gpio_pin);
    WRITE32_GPIO_REG(gpio_block, IMX_GPIO_DR, regVal);

    regVal = READ32_GPIO_REG(gpio_block, IMX_GPIO_GDIR);
    regVal &= ~(1 << gpio_pin);
    regVal |= (GPIO_OUTPUT << gpio_pin);
    WRITE32_GPIO_REG(gpio_block, IMX_GPIO_GDIR, regVal);
    mtx_unlock(&gpio->lock[gpio_block]);
    return ZX_OK;
}

zx_status_t imx8_gpio_read(void* ctx, uint32_t pin, uint8_t* out_value) {
    uint32_t gpio_block;
    uint32_t gpio_pin;
    uint32_t regVal;
    imx8_gpio_t* gpio = ctx;

    gpio_block = IMX_NUM_TO_BLOCK(pin);
    gpio_pin = IMX_NUM_TO_BIT(pin);

    if (gpio_block >= IMX_GPIO_BLOCKS || gpio_pin >= 32) {
        zxlogf(ERROR, "%s: Invalid GPIO pin (pin = %d Block = %d, Offset = %d)\n",
               __FUNCTION__, pin, gpio_block, gpio_pin);
        return ZX_ERR_INVALID_ARGS;
    }

    mtx_lock(&gpio->lock[gpio_block]);
    regVal = READ32_GPIO_REG(gpio_block, IMX_GPIO_DR);
    regVal >>= (gpio_pin);
    regVal &= 1;
    *out_value = regVal;
    mtx_unlock(&gpio->lock[gpio_block]);

    return ZX_OK;
}

zx_status_t imx8_gpio_write(void* ctx, uint32_t pin, uint8_t value) {
    uint32_t gpio_block;
    uint32_t gpio_pin;
    uint32_t regVal;
    imx8_gpio_t* gpio = ctx;

    gpio_block = IMX_NUM_TO_BLOCK(pin);
    gpio_pin = IMX_NUM_TO_BIT(pin);
    if (gpio_block >= IMX_GPIO_BLOCKS || gpio_pin >= 32) {
        zxlogf(ERROR, "%s: Invalid GPIO pin (pin = %d Block = %d, Offset = %d)\n",
               __FUNCTION__, pin, gpio_block, gpio_pin);
        return ZX_ERR_INVALID_ARGS;
    }

    mtx_lock(&gpio->lock[gpio_block]);
    regVal = READ32_GPIO_REG(gpio_block, IMX_GPIO_DR);
    regVal &= ~(1 << gpio_pin);
    regVal |= (value << gpio_pin);
    WRITE32_GPIO_REG(gpio_block, IMX_GPIO_DR, regVal);
    mtx_unlock(&gpio->lock[gpio_block]);

    return ZX_OK;
}

static void imx8_gpio_mask_irq(imx8_gpio_t* gpio, uint32_t gpio_block, uint32_t gpio_pin) {
    uint32_t regVal = READ32_GPIO_REG(gpio_block, IMX_GPIO_IMR);
    regVal &= ~(1 << gpio_pin);
    WRITE32_GPIO_REG(gpio_block, IMX_GPIO_IMR, regVal);
}

static void imx8_gpio_unmask_irq(imx8_gpio_t* gpio, uint32_t gpio_block, uint32_t gpio_pin) {
    uint32_t regVal = READ32_GPIO_REG(gpio_block, IMX_GPIO_IMR);
    regVal |= (1 << gpio_pin);
    WRITE32_GPIO_REG(gpio_block, IMX_GPIO_IMR, regVal);
}

int imx8_gpio_irq_handler(void* arg) {
    imx8_gpio_t* gpio = arg;
    zx_port_packet_t packet;
    zx_status_t status = ZX_OK;
    uint32_t gpio_block;
    uint32_t isr;
    uint32_t imr;
    uint32_t pin;

    while (1) {
        status = zx_port_wait(gpio->porth, ZX_TIME_INFINITE, &packet);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: zx_port_wait failed %d \n", __FUNCTION__, status);
            goto fail;
        }
        zxlogf(INFO, "GPIO Interrupt %x triggered\n", (unsigned int)packet.key);
        status = zx_interrupt_ack(gpio->inth[packet.key]);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: zx_interrupt_ack failed %d \n", __FUNCTION__, status);
            goto fail;
        }

        gpio_block = IMX_INT_NUM_TO_BLOCK(packet.key);
        isr = READ32_GPIO_REG(gpio_block, IMX_GPIO_ISR);

        imr = READ32_GPIO_REG(gpio_block, IMX_GPIO_IMR);

        // Get the status of the enabled interrupts
        // Get the last valid interrupt pin
        uint32_t valid_irqs = (isr & imr);
        if (valid_irqs) {
            pin = __builtin_ctz(valid_irqs);
            WRITE32_GPIO_REG(gpio_block, IMX_GPIO_ISR, 1 << pin);
            pin = gpio_block * IMX_GPIO_PER_BLOCK + pin;

            if (gpio->vinth[pin] != ZX_HANDLE_INVALID) {
                // Trigger the corresponding virtual interrupt
                status = zx_interrupt_trigger(gpio->vinth[pin], 0, zx_clock_get_monotonic());
                if (status != ZX_OK) {
                    zxlogf(ERROR, "%s: zx_interrupt_trigger failed %d \n", __FUNCTION__, status);
                    goto fail;
                }
            }
        }
    }

fail:
    for (int i = 0; i < IMX_GPIO_INTERRUPTS; i++) {
        zx_interrupt_destroy(gpio->inth[i]);
        zx_handle_close(gpio->inth[i]);
    }
    return status;
}

zx_status_t imx8_gpio_get_interrupt(void* ctx, uint32_t pin,
                                           uint32_t flags,
                                           zx_handle_t* out_handle) {
    uint32_t gpio_block;
    uint32_t gpio_pin;
    imx8_gpio_t* gpio = ctx;
    uint32_t regVal;
    uint32_t interrupt_type;
    zx_status_t status = ZX_OK;
    uint32_t icr_offset;

    gpio_block = IMX_NUM_TO_BLOCK(pin);
    gpio_pin = IMX_NUM_TO_BIT(pin);
    if (gpio_block >= IMX_GPIO_BLOCKS || gpio_pin >= IMX_GPIO_PER_BLOCK) {
        zxlogf(ERROR, "%s: Invalid GPIO pin (pin = %d Block = %d, Offset = %d)\n",
               __FUNCTION__, pin, gpio_block, gpio_pin);
        return ZX_ERR_INVALID_ARGS;
    }

    // Create Virtual Interrupt
    status = zx_interrupt_create(0, 0, ZX_INTERRUPT_VIRTUAL, &gpio->vinth[pin]);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: zx_irq_create failed %d \n", __FUNCTION__, status);
        return status;
    }

    // Store the Virtual Interrupt
    status = zx_handle_duplicate(gpio->vinth[pin], ZX_RIGHT_SAME_RIGHTS, out_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: zx_handle_duplicate failed %d \n", __FUNCTION__, status);
        return status;
    }

    mtx_lock(&gpio->lock[gpio_block]);
    // Select EGDE or LEVEL and polarity
    switch (flags & ZX_INTERRUPT_MODE_MASK) {
    case ZX_INTERRUPT_MODE_EDGE_LOW:
        interrupt_type = IMX_GPIO_FALLING_EDGE_INTERRUPT;
        break;
    case ZX_INTERRUPT_MODE_EDGE_HIGH:
        interrupt_type = IMX_GPIO_RISING_EDGE_INTERRUPT;
        break;
    case ZX_INTERRUPT_MODE_LEVEL_LOW:
        interrupt_type = IMX_GPIO_LOW_LEVEL_INTERRUPT;
        break;
    case ZX_INTERRUPT_MODE_LEVEL_HIGH:
        interrupt_type = IMX_GPIO_HIGH_LEVEL_INTERRUPT;
        break;
    case ZX_INTERRUPT_MODE_EDGE_BOTH:
        interrupt_type = IMX_GPIO_BOTH_EDGE_INTERRUPT;
        break;
    default:
        status = ZX_ERR_INVALID_ARGS;
        goto fail;
    }

    if (interrupt_type == IMX_GPIO_BOTH_EDGE_INTERRUPT) {
        regVal = READ32_GPIO_REG(gpio_block, IMX_GPIO_EDGE_SEL);
        regVal |= (1 << gpio_pin);
        WRITE32_GPIO_REG(gpio_block, IMX_GPIO_EDGE_SEL, regVal);
    } else {
        // Select which ICR register to program
        if (gpio_pin >= IMX_GPIO_MAX_ICR_PIN) {
            icr_offset = IMX_GPIO_ICR2;
        } else {
            icr_offset = IMX_GPIO_ICR1;
        }
        regVal = READ32_GPIO_REG(gpio_block, icr_offset);
        regVal &= ~(IMX_GPIO_ICR_MASK << IMX_GPIO_ICR_SHIFT(gpio_pin));
        regVal |= (interrupt_type << IMX_GPIO_ICR_SHIFT(gpio_pin));
        WRITE32_GPIO_REG(gpio_block, icr_offset, regVal);
    }

    // Mask the Interrupt
    imx8_gpio_mask_irq(gpio, gpio_block, gpio_pin);

    // Clear the Interrupt Status
    WRITE32_GPIO_REG(gpio_block, IMX_GPIO_ISR, 1 << gpio_pin);

    // Unmask the Interrupt
    imx8_gpio_unmask_irq(gpio, gpio_block, gpio_pin);

fail:
    mtx_unlock(&gpio->lock[gpio_block]);
    return status;
}

zx_status_t imx8_gpio_release_interrupt(void* ctx, uint32_t pin) {
    imx8_gpio_t* gpio = ctx;
    zx_status_t status = ZX_OK;
    uint32_t gpio_pin = IMX_NUM_TO_BIT(pin);
    uint32_t gpio_block = IMX_NUM_TO_BLOCK(pin);
    if (gpio_block >= IMX_GPIO_BLOCKS || gpio_pin >= IMX_GPIO_PER_BLOCK) {
        zxlogf(ERROR, "%s: Invalid GPIO pin (pin = %d Block = %d, Offset = %d)\n",
               __FUNCTION__, pin, gpio_block, gpio_pin);
        return ZX_ERR_INVALID_ARGS;
    }
    mtx_lock(&gpio->gpio_lock);
    // Mask the interrupt
    imx8_gpio_mask_irq(gpio, gpio_block, gpio_pin);

    zx_handle_close(gpio->vinth[pin]);
    gpio->vinth[pin] = ZX_HANDLE_INVALID;
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: zx_handle_close failed %d \n", __FUNCTION__, status);
        goto fail;
    }

fail:
    mtx_unlock(&gpio->gpio_lock);
    return status;
}

zx_status_t imx8_gpio_set_polarity(void* ctx, uint32_t pin,
                                          uint32_t polarity) {
    return ZX_ERR_NOT_SUPPORTED;
}
