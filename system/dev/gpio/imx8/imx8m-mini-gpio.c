// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "imx8-gpio.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform-device-lib.h>
#include <soc/imx8m-mini/imx8m-mini-iomux.h>
#include <stdlib.h>

// Configure a pin for an alternate function specified by fn
static zx_status_t imx8m_mini_gpio_set_alt_function(void* ctx, const uint32_t pin,
                                                    const uint64_t fn) {
    imx8_gpio_t* gpio = ctx;
    iomux_cfg_struct s_cfg = (iomux_cfg_struct)fn;

    volatile uint8_t* iomux = (volatile uint8_t*)gpio->mmio_iomux.vaddr;

    zxlogf(SPEW, "0x%lx\n", s_cfg);
    zxlogf(SPEW, "val = 0x%lx, reg = %p\n",
           IOMUX_CFG_MUX_MODE_VAL(GET_MUX_MODE_VAL(s_cfg)) |
               IOMUX_CFG_SION_VAL(GET_SION_VAL(s_cfg)),
           iomux + GET_MUX_CTL_OFF_VAL(s_cfg));
    zxlogf(SPEW, "val = 0x%lx, reg = %p\n",
           IOMUX_CFG_DSE_VAL(GET_DSE_VAL(s_cfg)) |
               IOMUX_CFG_FSEL_VAL(GET_FSEL_VAL(s_cfg)) |
               IOMUX_CFG_ODE_VAL(GET_ODE_VAL(s_cfg)) |
               IOMUX_CFG_PUE_VAL(GET_PUE_VAL(s_cfg)) |
               IOMUX_CFG_HYS_VAL(GET_HYS_VAL(s_cfg)) |
               IOMUX_CFG_PE_VAL(GET_PE_VAL(s_cfg)),
           iomux + GET_PAD_CTL_OFF_VAL(s_cfg));
    zxlogf(SPEW, "val = 0x%lx, reg = %p\n",
           IOMUX_CFG_DAISY_VAL(GET_DAISY_VAL(s_cfg)),
           iomux + GET_SEL_INP_OFF_VAL(s_cfg));

    if (GET_MUX_CTL_OFF_VAL(s_cfg)) {
        writel(
            IOMUX_CFG_MUX_MODE_VAL(GET_MUX_MODE_VAL(s_cfg)) |
                IOMUX_CFG_SION_VAL(GET_SION_VAL(s_cfg)),
            iomux + GET_MUX_CTL_OFF_VAL(s_cfg));
    }
    if (GET_PAD_CTL_OFF_VAL(s_cfg)) {
        writel(
            IOMUX_CFG_DSE_VAL(GET_DSE_VAL(s_cfg)) |
                IOMUX_CFG_FSEL_VAL(GET_FSEL_VAL(s_cfg)) |
                IOMUX_CFG_ODE_VAL(GET_ODE_VAL(s_cfg)) |
                IOMUX_CFG_PUE_VAL(GET_PUE_VAL(s_cfg)) |
                IOMUX_CFG_HYS_VAL(GET_HYS_VAL(s_cfg)) |
                IOMUX_CFG_PE_VAL(GET_PE_VAL(s_cfg)),
            iomux + GET_PAD_CTL_OFF_VAL(s_cfg));
    }
    if (GET_SEL_INP_OFF_VAL(s_cfg)) {
        writel(IOMUX_CFG_DAISY_VAL(GET_DAISY_VAL(s_cfg)),
               iomux + GET_SEL_INP_OFF_VAL(s_cfg));
    }

    return ZX_OK;
}

static gpio_impl_protocol_ops_t gpio_ops = {
    .config_in = imx8_gpio_config_in,
    .config_out = imx8_gpio_config_out,
    .set_alt_function = imx8m_mini_gpio_set_alt_function,
    .read = imx8_gpio_read,
    .write = imx8_gpio_write,
    .get_interrupt = imx8_gpio_get_interrupt,
    .release_interrupt = imx8_gpio_release_interrupt,
    .set_polarity = imx8_gpio_set_polarity,
};

static void imx8m_mini_gpio_release(void* ctx) {
    unsigned i;
    imx8_gpio_t* gpio = ctx;
    mtx_lock(&gpio->gpio_lock);
    for (i = 0; i < IMX_GPIO_BLOCKS; i++) {
        mmio_buffer_release(&gpio->mmios[i]);
    }
    mmio_buffer_release(&gpio->mmio_iomux);

    for (int i = 0; i < IMX_GPIO_INTERRUPTS; i++) {
        zx_interrupt_destroy(gpio->inth[i]);
        zx_handle_close(gpio->inth[i]);
    }
    free(gpio);
    mtx_unlock(&gpio->gpio_lock);
}

static zx_protocol_device_t gpio_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = imx8m_mini_gpio_release,
};

static zx_status_t imx8m_mini_gpio_bind(void* ctx, zx_device_t* parent) {
    zx_status_t status;
    unsigned i;

    imx8_gpio_t* gpio = calloc(1, sizeof(imx8_gpio_t));
    if (!gpio) {
        return ZX_ERR_NO_MEMORY;
    }

    status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &gpio->pdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: ZX_PROTOCOL_PLATFORM_DEV not available %d \n", __FUNCTION__, status);
        goto fail;
    }

    status = device_get_protocol(parent, ZX_PROTOCOL_PBUS, &gpio->pbus);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: ZX_PROTOCOL_PBUS not available %d\n", __FUNCTION__, status);
        goto fail;
    }

    for (i = 0; i < IMX_GPIO_BLOCKS; i++) {
        status = pdev_map_mmio_buffer2(&gpio->pdev, i, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                       &gpio->mmios[i]);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: pdev_map_mmio_buffer gpio failed %d\n", __FUNCTION__, status);
            goto fail;
        }

        mtx_init(&gpio->lock[i], mtx_plain);
    }

    status = pdev_map_mmio_buffer2(&gpio->pdev, IMX_GPIO_BLOCKS, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                   &gpio->mmio_iomux);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_map_mmio_buffer iomux failed %d\n", __FUNCTION__, status);
        goto fail;
    }

    pdev_device_info_t info;
    status = pdev_get_device_info(&gpio->pdev, &info);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_get_device_info failed %d\n", __FUNCTION__, status);
        goto fail;
    }

    status = zx_port_create(ZX_PORT_BIND_TO_INTERRUPT, &gpio->porth);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: zx_port_create failed %d\n", __FUNCTION__, status);
        goto fail;
    }

    for (i = 0; i < info.irq_count; i++) {
        // Create Interrupt Object
        status = pdev_map_interrupt(&gpio->pdev, i,
                                    &gpio->inth[i]);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: pdev_map_interrupt failed %d\n", __FUNCTION__, status);
            goto fail;
        }
        // The KEY is the Interrupt Number for our usecase
        status = zx_interrupt_bind(gpio->inth[i], gpio->porth, i, 0 /*optons*/);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: zx_interrupt_bind failed %d\n", __FUNCTION__, status);
            goto fail;
        }
    }

    thrd_create_with_name(&gpio->irq_handler, imx8_gpio_irq_handler, gpio,
                          "imx8m_mini_gpio_irq_handler");

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "imx8m-mini-gpio",
        .ctx = gpio,
        .ops = &gpio_device_proto,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    status = device_add(parent, &args, &gpio->zxdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: device_add failed! %d\n", __FUNCTION__, status);
        goto fail;
    }

    gpio->gpio.ops = &gpio_ops;
    gpio->gpio.ctx = gpio;
    const platform_proxy_cb_t kCallback = {NULL, NULL};
    pbus_register_protocol(&gpio->pbus, ZX_PROTOCOL_GPIO_IMPL, &gpio->gpio, sizeof(gpio->gpio),
                           &kCallback);

    return ZX_OK;

fail:
    imx8m_mini_gpio_release(gpio);
    return status;
}

static zx_driver_ops_t imx8m_mini_gpio_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = imx8m_mini_gpio_bind,
};

ZIRCON_DRIVER_BEGIN(imx8m_mini_gpio, imx8m_mini_gpio_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_NXP),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_DID, PDEV_DID_IMX_GPIO),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_IMX8MMEVK),
ZIRCON_DRIVER_END(imx8m_mini_gpio)
