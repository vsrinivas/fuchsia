// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/platform-defs.h>

#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include "gauss.h"
#include "gauss-hw.h"

#define GAUSS_TDM_SAMPLE_RATE (48000)
#define GAUSS_TDM_BITS_PER_SLOT (32)
#define GAUSS_TDM_SLOTS_PER_FRAME (8)
#define GAUSS_TDM_CLK_SRC_MULT (20)
#define GAUSS_TDM_CLK_N        (GAUSS_TDM_BITS_PER_SLOT * \
                                GAUSS_TDM_SLOTS_PER_FRAME * \
                                GAUSS_TDM_CLK_SRC_MULT)


// 48khz sample rate, 8 slots, 32bits per slot
#define GAUSS_TDM_CLK_SRC_FREQ (GAUSS_TDM_SAMPLE_RATE * \
                                GAUSS_TDM_CLK_N)

// turn this on to enable Gauss accelerometer test driver
//#define I2C_TEST 1

#if I2C_TEST
static const pbus_i2c_channel_t i2c_test_channels[] = {
    {
        // Gauss accelerometer
        .bus_id = AML_I2C_B,
        .address = 0x18,
    },
};

static const pbus_dev_t i2c_test_dev = {
    .name = "i2c-test",
    .vid = PDEV_VID_GOOGLE,
    .pid = PDEV_PID_GAUSS,
    .did = PDEV_DID_GAUSS_I2C_TEST,
    .i2c_channels = i2c_test_channels,
    .i2c_channel_count = countof(i2c_test_channels),
};
#endif

static const pbus_i2c_channel_t led_i2c_channels[] = {
    {
        .bus_id = AML_I2C_A,
        .address = 0x3f,
    },
};

static const pbus_dev_t led_dev = {
    .name = "led",
    .vid = PDEV_VID_GOOGLE,
    .pid = PDEV_PID_GAUSS,
    .did = PDEV_DID_GAUSS_LED,
    .i2c_channels = led_i2c_channels,
    .i2c_channel_count = countof(led_i2c_channels),
};

static void gauss_bus_release(void* ctx) {
    gauss_bus_t* bus = ctx;
    io_buffer_release(&bus->usb_phy);
    zx_handle_close(bus->bti_handle);
    free(bus);
}

static zx_protocol_device_t gauss_bus_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .release = gauss_bus_release,
};

#if 0
static aml_i2c_dev_desc_t i2c_devs[] = {
    {.port = AML_I2C_A, .base_phys = 0xffd1f000, .irqnum = (21+32)},
    {.port = AML_I2C_B, .base_phys = 0xffd1e000, .irqnum = (214+32)},
    // Gauss only uses I2C_A and I2C_B
/*
    {.port = AML_I2C_C, .base_phys = 0xffd1d000, .irqnum = (215+32)},
    {.port = AML_I2C_D, .base_phys = 0xffd1c000, .irqnum = (39+32)},
*/
};
#endif

static int gauss_start_thread(void* arg) {
    gauss_bus_t* bus = arg;
    zx_status_t status;

    if ((status = gauss_clk_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "gauss_clk_init failed: %d\n", status);
        goto fail;
    }

    if ((status = gauss_gpio_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "gauss_gpio_init failed: %d\n", status);
        goto fail;
    }

    // pinmux for Gauss i2c
    gpio_set_alt_function(&bus->gpio, I2C_SCK_A, 1);
    gpio_set_alt_function(&bus->gpio, I2C_SDA_A, 1);
    gpio_set_alt_function(&bus->gpio, I2C_SCK_B, 1);
    gpio_set_alt_function(&bus->gpio, I2C_SDA_B, 1);

    // Config pinmux for gauss PDM pins
    gpio_set_alt_function(&bus->gpio, A113_GPIOA(14), 1);
    gpio_set_alt_function(&bus->gpio, A113_GPIOA(15), 1);
    gpio_set_alt_function(&bus->gpio, A113_GPIOA(16), 1);
    gpio_set_alt_function(&bus->gpio, A113_GPIOA(17), 1);
    gpio_set_alt_function(&bus->gpio, A113_GPIOA(18), 1);

    gpio_set_alt_function(&bus->gpio, TDM_BCLK_C, 1);
    gpio_set_alt_function(&bus->gpio, TDM_FSYNC_C, 1);
    gpio_set_alt_function(&bus->gpio, TDM_MOSI_C, 1);
    gpio_set_alt_function(&bus->gpio, TDM_MISO_C, 2);

    gpio_set_alt_function(&bus->gpio, SPK_MUTEn, 0);
    gpio_config(&bus->gpio, SPK_MUTEn, GPIO_DIR_OUT);
    gpio_write(&bus->gpio, SPK_MUTEn, 1);

    if ((status = gauss_i2c_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "gauss_i2c_init failed: %d\n", status);
        goto fail;
    }

    status = a113_clk_init(bus->bti_handle, &bus->clocks);
    if (status != ZX_OK) {
        zxlogf(ERROR, "a113_clk_init failed: %d\n",status);
        goto fail;
    }

    // Set mpll2 to 20x our desired sckl frequency.
    // tdm divides down by 20 for sclk
    uint64_t actual_freq;
    a113_clk_set_mpll2(bus->clocks, GAUSS_TDM_CLK_SRC_FREQ, &actual_freq);

    zxlogf(INFO,"Requested sample rate = %d, actual = %ld\n",GAUSS_TDM_SAMPLE_RATE,
                                                       actual_freq / GAUSS_TDM_CLK_N);

    if ((status = gauss_pcie_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "gauss_pcie_init failed: %d\n", status);
        goto fail;
    }
    if ((status = gauss_usb_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "gauss_usb_init failed: %d\n", status);
        goto fail;
    }
    if ((status = gauss_audio_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "gauss_audio_init failed: %d\n", status);
        goto fail;
    }

#if I2C_TEST
    if ((status = pbus_device_add(&bus->pbus, &i2c_test_dev, 0)) != ZX_OK) {
        zxlogf(ERROR, "a113_i2c_init could not add i2c_test_dev: %d\n", status);
        goto fail;
    }
#endif

    if ((status = gauss_raw_nand_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "gauss_raw_nand_init failed: %d\n", status);
        goto fail;
    }

    if ((status = pbus_device_add(&bus->pbus, &led_dev, 0)) != ZX_OK) {
        zxlogf(ERROR, "a113_i2c_init could not add i2c_led_dev: %d\n", status);
        goto fail;
    }

    return ZX_OK;
fail:
    zxlogf(ERROR, "gauss_start_thread failed, not all devices have been initialized\n");
    return status;
}

static zx_status_t gauss_bus_bind(void* ctx, zx_device_t* parent) {
    gauss_bus_t* bus = calloc(1, sizeof(gauss_bus_t));
    if (!bus) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_BUS, &bus->pbus);
    if (status != ZX_OK) {
        goto fail;
    }

    // get dummy IOMMU implementation in the platform bus
    status = device_get_protocol(parent, ZX_PROTOCOL_IOMMU, &bus->iommu);
    if (status != ZX_OK) {
        zxlogf(ERROR, "gauss_bus_bind: could not get ZX_PROTOCOL_IOMMU\n");
        goto fail;
    }
    status = iommu_get_bti(&bus->iommu, 0, BTI_BOARD, &bus->bti_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "gauss_bus_bind: iommu_get_bti failed: %d\n", status);
        goto fail;
    }

    bus->parent = parent;

   device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "gauss-bus",
        .ctx = bus,
        .ops = &gauss_bus_device_protocol,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    status = device_add(parent, &args, NULL);
    if (status != ZX_OK) {
        goto fail;
    }

    thrd_t t;
    int thrd_rc = thrd_create_with_name(&t, gauss_start_thread, bus, "gauss_start_thread");
    if (thrd_rc != thrd_success) {
        goto fail;
    }
    return ZX_OK;

fail:
    printf("gauss_bus_bind failed %d\n", status);
    gauss_bus_release(bus);
    return status;
}

static zx_driver_ops_t gauss_bus_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = gauss_bus_bind,
};

ZIRCON_DRIVER_BEGIN(gauss_bus, gauss_bus_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_BUS),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GOOGLE),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_GAUSS),
ZIRCON_DRIVER_END(gauss_bus)
