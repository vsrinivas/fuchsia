// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <threads.h>

#include <ddk/device.h>
#include <ddk/mmio-buffer.h>
#include <ddk/protocol/gpio-impl.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-device.h>
#include <hw/reg.h>
#include <soc/imx8m/imx8m-gpio.h>
#include <zircon/syscalls/port.h>

typedef struct {
    pdev_protocol_t pdev;
    pbus_protocol_t pbus;
    gpio_impl_protocol_t gpio;
    zx_device_t* zxdev;
    mmio_buffer_t mmios[IMX_GPIO_BLOCKS];
    mmio_buffer_t mmio_iomux;
    mtx_t lock[IMX_GPIO_BLOCKS];
    zx_handle_t inth[IMX_GPIO_INTERRUPTS];
    zx_handle_t vinth[IMX_GPIO_MAX];
    zx_handle_t porth;
    thrd_t irq_handler;
    mtx_t gpio_lock;
} imx8_gpio_t;

#define READ32_GPIO_REG(block_index, offset) \
        readl((uint8_t*)gpio->mmios[block_index].vaddr + offset)
#define WRITE32_GPIO_REG(block_index, offset, value) \
        writel(value, (uint8_t*)gpio->mmios[block_index].vaddr + offset)

zx_status_t imx8_gpio_config_in(void* ctx, uint32_t pin, uint32_t flags);
zx_status_t imx8_gpio_config_out(void* ctx, uint32_t pin, uint8_t initial_value);
zx_status_t imx8_gpio_read(void* ctx, uint32_t pin, uint8_t* out_value);
zx_status_t imx8_gpio_write(void* ctx, uint32_t pin, uint8_t value);
zx_status_t imx8_gpio_get_interrupt(void* ctx, uint32_t pin, uint32_t flags,
                                    zx_handle_t* out_handle);
zx_status_t imx8_gpio_release_interrupt(void* ctx, uint32_t pin);
zx_status_t imx8_gpio_set_polarity(void* ctx, uint32_t pin, uint32_t polarity);
/* imx8_set_alt_function is SoC dependent. */
int imx8_gpio_irq_handler(void* arg);
