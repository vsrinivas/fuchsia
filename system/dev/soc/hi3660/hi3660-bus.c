// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/gpio.h>

#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <magenta/assert.h>

#include "pl061.h"
#include "hi3660-bus.h"

// MMIO indices
enum {
    // GPIO MMIOs precede these
    MMIO_USB3OTG_BC = 5,
    MMIO_PERI_CRG,
    MMIO_PCTRL,
};

static pl061_gpios_t* find_gpio(hi3660_bus_t* bus, unsigned pin) {
    pl061_gpios_t* gpios;
    // TODO(voydanoff) consider using a fancier data structure here
    list_for_every_entry(&bus->gpios, gpios, pl061_gpios_t, node) {
        if (pin >= gpios->gpio_start && pin < gpios->gpio_start + gpios->gpio_count) {
            return gpios;
        }
    }
    printf("find_gpio failed for pin %u\n", pin);
    return NULL;
}

static mx_status_t hi3660_gpio_config(void* ctx, unsigned pin, gpio_config_flags_t flags) {
    hi3660_bus_t* bus = ctx;
    pl061_gpios_t* gpios = find_gpio(bus, pin);
    if (!gpios) {
        return MX_ERR_INVALID_ARGS;
    }
    return pl061_proto_ops.config(gpios, pin, flags);
}

static mx_status_t hi3660_gpio_read(void* ctx, unsigned pin, unsigned* out_value) {
    hi3660_bus_t* bus = ctx;
    pl061_gpios_t* gpios = find_gpio(bus, pin);
    if (!gpios) {
        return MX_ERR_INVALID_ARGS;
    }
    return pl061_proto_ops.read(gpios, pin, out_value);
}

static mx_status_t hi3660_gpio_write(void* ctx, unsigned pin, unsigned value) {
    hi3660_bus_t* bus = ctx;
    pl061_gpios_t* gpios = find_gpio(bus, pin);
    if (!gpios) {
        return MX_ERR_INVALID_ARGS;
    }
    return pl061_proto_ops.write(gpios, pin, value);
}

static mx_status_t hi3660_gpio_int_enable(void* ctx, unsigned pin, bool enable) {
    hi3660_bus_t* bus = ctx;
    pl061_gpios_t* gpios = find_gpio(bus, pin);
    if (!gpios) {
        return MX_ERR_INVALID_ARGS;
    }
    return pl061_proto_ops.int_enable(gpios, pin, enable);
}

static mx_status_t hi3660_gpio_get_int_status(void* ctx, unsigned pin, bool* out_status) {
    hi3660_bus_t* bus = ctx;
    pl061_gpios_t* gpios = find_gpio(bus, pin);
    if (!gpios) {
        return MX_ERR_INVALID_ARGS;
    }
    return pl061_proto_ops.get_int_status(gpios, pin, out_status);
}

static mx_status_t hi3660_gpio_int_clear(void* ctx, unsigned pin) {
    hi3660_bus_t* bus = ctx;
    pl061_gpios_t* gpios = find_gpio(bus, pin);
    if (!gpios) {
        return MX_ERR_INVALID_ARGS;
    }
    return pl061_proto_ops.int_clear(gpios, pin);
}

static gpio_protocol_ops_t gpio_ops = {
    .config = hi3660_gpio_config,
    .read = hi3660_gpio_read,
    .write = hi3660_gpio_write,
    .int_enable = hi3660_gpio_int_enable,
    .get_int_status = hi3660_gpio_get_int_status,
    .int_clear = hi3660_gpio_int_clear,
};

static mx_status_t hi3660_get_protocol(void* ctx, uint32_t proto_id, void* out) {
    if (proto_id == MX_PROTOCOL_GPIO) {
        gpio_protocol_t* proto = out;
        proto->ctx = ctx;
        proto->ops = &gpio_ops;
        return MX_OK;
    }
    return MX_ERR_NOT_SUPPORTED;
}

static mx_status_t hi3660_add_gpios(void* ctx, uint32_t start, uint32_t count, uint32_t mmio_index,
                                    const uint32_t* irqs, uint32_t irq_count) {
    hi3660_bus_t* bus = ctx;

    pl061_gpios_t* gpios = calloc(1, sizeof(pl061_gpios_t));
    if (!gpios) {
        return MX_ERR_NO_MEMORY;
    }

    mx_status_t status = pdev_map_mmio_buffer(&bus->pdev, mmio_index,
                                              MX_CACHE_POLICY_UNCACHED_DEVICE, &gpios->buffer);
    if (status != MX_OK) {
        free(gpios);
        return status;
    }

    mtx_init(&gpios->lock, mtx_plain);
    gpios->gpio_start = start;
    gpios->gpio_count = count;
    gpios->irqs = irqs;
    gpios->irq_count = irq_count;
    list_add_tail(&bus->gpios, &gpios->node);

    return MX_OK;
}

static pbus_interface_ops_t hi3660_bus_ops = {
    .get_protocol = hi3660_get_protocol,
    .add_gpios = hi3660_add_gpios,
};

static void hi3660_release(void* ctx) {
    hi3660_bus_t* bus = ctx;
    pl061_gpios_t* gpios;

    while ((gpios = list_remove_head_type(&bus->gpios, pl061_gpios_t, node)) != NULL) {
        pdev_mmio_buffer_release(&gpios->buffer);
        free(gpios);
    }

    pdev_mmio_buffer_release(&bus->usb3otg_bc);
    pdev_mmio_buffer_release(&bus->peri_crg);
    pdev_mmio_buffer_release(&bus->pctrl);

    free(bus);
}

static mx_protocol_device_t hi3660_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .release = hi3660_release,
};

// test thread that cycles the 4 LEDs on the hikey 960 board
static int led_test_thread(void *arg) {
    mx_device_t* parent = arg;

    platform_device_protocol_t pdev;
    if (device_get_protocol(parent, MX_PROTOCOL_PLATFORM_DEV, &pdev) != MX_OK) {
        printf("led_test_thread: could not get pdev protocol!\n");
        return MX_ERR_INTERNAL;
    }

    gpio_protocol_t gpio;
    if (pdev_get_protocol(&pdev, MX_PROTOCOL_GPIO, &gpio) != MX_OK) {
        printf("led_test_thread: could not get GPIO protocol!\n");
        return MX_ERR_INTERNAL;
    }

    uint32_t led_gpios[] = { 150, 151, 190, 189 };

    for (unsigned i = 0; i < countof(led_gpios); i++) {
        gpio_config(&gpio, led_gpios[i], GPIO_DIR_OUT);
    }

    while (1) {
         for (unsigned i = 0; i < countof(led_gpios); i++) {
            gpio_write(&gpio, led_gpios[i], 1);
            sleep(1);
            gpio_write(&gpio, led_gpios[i], 0);
        }
    }

    return 0;
}

static mx_status_t hi3660_bind(void* ctx, mx_device_t* parent, void** cookie) {
    platform_device_protocol_t pdev;
    if (device_get_protocol(parent, MX_PROTOCOL_PLATFORM_DEV, &pdev) != MX_OK) {
        return MX_ERR_NOT_SUPPORTED;
    }

    hi3660_bus_t* bus = calloc(1, sizeof(hi3660_bus_t));
    if (!bus) {
        return MX_ERR_NO_MEMORY;
    }

    list_initialize(&bus->gpios);
    memcpy(&bus->pdev, &pdev, sizeof(bus->pdev));

    mx_status_t status;
    if ((status = pdev_map_mmio_buffer(&pdev, MMIO_USB3OTG_BC, MX_CACHE_POLICY_UNCACHED_DEVICE,
                                       &bus->usb3otg_bc)) != MX_OK ||
         (status = pdev_map_mmio_buffer(&pdev, MMIO_PERI_CRG, MX_CACHE_POLICY_UNCACHED_DEVICE,
                                       &bus->peri_crg)) != MX_OK ||
         (status = pdev_map_mmio_buffer(&pdev, MMIO_PCTRL, MX_CACHE_POLICY_UNCACHED_DEVICE,
                                       &bus->pctrl)) != MX_OK) {
        goto fail;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "hi3660-bus",
        .ops = &hi3660_device_protocol,
        // nothing should bind to this device
        // all interaction will be done via the pbus_interface_t
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    status = device_add(parent, &args, NULL);
    if (status != MX_OK) {
        goto fail;
    }

    pbus_interface_t intf;
    intf.ops = &hi3660_bus_ops;
    intf.ctx = bus;
    pdev_set_interface(&pdev, &intf);

#if 0
    thrd_t thrd;
    thrd_create_with_name(&thrd, led_test_thread, parent, "led_test_thread");
#endif

    // must be after pdev_set_interface
    if ((status = hi3360_usb_init(bus)) != MX_OK) {
        printf("hi3660_bind: hi3360_usb_init failed!\n");;
    }

    return MX_OK;

fail:
    printf("hi3660_bind failed %d\n", status);
    hi3660_release(bus);
    return status;
}

static mx_driver_ops_t hi3660_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = hi3660_bind,
};

MAGENTA_DRIVER_BEGIN(hi3660, hi3660_driver_ops, "magenta", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, 0x12D1),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, 0x0960),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_BUS_IMPLEMENTOR_DID),
MAGENTA_DRIVER_END(hi3660)
