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

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/platform-defs.h>

#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/assert.h>

#include "pl061.h"
#include "hi3660-bus.h"
#include "hi3660-hw.h"
#include "hikey960-hw.h"

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

static zx_status_t hi3660_gpio_config(void* ctx, unsigned pin, gpio_config_flags_t flags) {
    hi3660_bus_t* bus = ctx;
    pl061_gpios_t* gpios = find_gpio(bus, pin);
    if (!gpios) {
        return ZX_ERR_INVALID_ARGS;
    }
    return pl061_proto_ops.config(gpios, pin, flags);
}

static zx_status_t hi3660_gpio_read(void* ctx, unsigned pin, unsigned* out_value) {
    hi3660_bus_t* bus = ctx;
    pl061_gpios_t* gpios = find_gpio(bus, pin);
    if (!gpios) {
        return ZX_ERR_INVALID_ARGS;
    }
    return pl061_proto_ops.read(gpios, pin, out_value);
}

static zx_status_t hi3660_gpio_write(void* ctx, unsigned pin, unsigned value) {
    hi3660_bus_t* bus = ctx;
    pl061_gpios_t* gpios = find_gpio(bus, pin);
    if (!gpios) {
        return ZX_ERR_INVALID_ARGS;
    }
    return pl061_proto_ops.write(gpios, pin, value);
}

static zx_status_t hi3660_gpio_int_enable(void* ctx, unsigned pin, bool enable) {
    hi3660_bus_t* bus = ctx;
    pl061_gpios_t* gpios = find_gpio(bus, pin);
    if (!gpios) {
        return ZX_ERR_INVALID_ARGS;
    }
    return pl061_proto_ops.int_enable(gpios, pin, enable);
}

static zx_status_t hi3660_gpio_get_int_status(void* ctx, unsigned pin, bool* out_status) {
    hi3660_bus_t* bus = ctx;
    pl061_gpios_t* gpios = find_gpio(bus, pin);
    if (!gpios) {
        return ZX_ERR_INVALID_ARGS;
    }
    return pl061_proto_ops.get_int_status(gpios, pin, out_status);
}

static zx_status_t hi3660_gpio_int_clear(void* ctx, unsigned pin) {
    hi3660_bus_t* bus = ctx;
    pl061_gpios_t* gpios = find_gpio(bus, pin);
    if (!gpios) {
        return ZX_ERR_INVALID_ARGS;
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

static zx_status_t hi3660_get_initial_mode(void* ctx, usb_mode_t* out_mode) {
    *out_mode = USB_MODE_DEVICE;
    return ZX_OK;
}

static zx_status_t hi3660_set_mode(void* ctx, usb_mode_t mode) {
    hi3660_bus_t* bus = ctx;

    if (mode == USB_MODE_OTG) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    return hi3660_usb_set_mode(bus, mode);
}

usb_mode_switch_protocol_ops_t usb_mode_switch_ops = {
    .get_initial_mode = hi3660_get_initial_mode,
    .set_mode = hi3660_set_mode,
};

static zx_status_t hi3660_get_protocol(void* ctx, uint32_t proto_id, void* out) {
    hi3660_bus_t* bus = ctx;

    switch (proto_id) {
    case ZX_PROTOCOL_GPIO: {
        memcpy(out, &bus->gpio, sizeof(bus->gpio));
        return ZX_OK;
    }
    case ZX_PROTOCOL_USB_MODE_SWITCH: {
        memcpy(out, &bus->usb_mode_switch, sizeof(bus->usb_mode_switch));
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static pbus_interface_ops_t hi3660_bus_ops = {
    .get_protocol = hi3660_get_protocol,
};

static void hi3660_release(void* ctx) {
    hi3660_bus_t* bus = ctx;
    pl061_gpios_t* gpios;

    while ((gpios = list_remove_head_type(&bus->gpios, pl061_gpios_t, node)) != NULL) {
        io_buffer_release(&gpios->buffer);
        free(gpios);
    }

    io_buffer_release(&bus->usb3otg_bc);
    io_buffer_release(&bus->peri_crg);
    io_buffer_release(&bus->pctrl);

    free(bus);
}

static zx_protocol_device_t hi3660_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .release = hi3660_release,
};

// test thread that cycles the 4 LEDs on the hikey 960 board
static int led_test_thread(void *arg) {
    hi3660_bus_t* bus = arg;
    gpio_protocol_t* gpio = &bus->gpio;

    uint32_t led_gpios[] = { GPIO_USER_LED1, GPIO_USER_LED2, GPIO_USER_LED3, GPIO_USER_LED4 };

    for (unsigned i = 0; i < countof(led_gpios); i++) {
        gpio_config(gpio, led_gpios[i], GPIO_DIR_OUT);
    }

    while (1) {
         for (unsigned i = 0; i < countof(led_gpios); i++) {
            gpio_write(gpio, led_gpios[i], 1);
            sleep(1);
            gpio_write(gpio, led_gpios[i], 0);
        }
    }

    return 0;
}

static zx_status_t hi3660_bind(void* ctx, zx_device_t* parent, void** cookie) {
    hi3660_bus_t* bus = calloc(1, sizeof(hi3660_bus_t));
    if (!bus) {
        return ZX_ERR_NO_MEMORY;
    }

    if (device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_BUS, &bus->pbus) != ZX_OK) {
        free(bus);
        return ZX_ERR_NOT_SUPPORTED;
    }

    list_initialize(&bus->gpios);
    bus->usb_mode = USB_MODE_NONE;

    // TODO(voydanoff) get from platform bus driver somehow
    zx_handle_t resource = get_root_resource();
    zx_status_t status;
    if ((status = io_buffer_init_physical(&bus->usb3otg_bc, MMIO_USB3OTG_BC_BASE,
                                          MMIO_USB3OTG_BC_LENGTH, resource,
                                          ZX_CACHE_POLICY_UNCACHED_DEVICE)) != ZX_OK ||
         (status = io_buffer_init_physical(&bus->peri_crg, MMIO_PERI_CRG_BASE, MMIO_PERI_CRG_LENGTH,
                                           resource, ZX_CACHE_POLICY_UNCACHED_DEVICE)) != ZX_OK ||
         (status = io_buffer_init_physical(&bus->pctrl, MMIO_PCTRL_BASE, MMIO_PCTRL_LENGTH,
                                           resource, ZX_CACHE_POLICY_UNCACHED_DEVICE)) != ZX_OK) {
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
    if (status != ZX_OK) {
        goto fail;
    }

    bus->gpio.ops = &gpio_ops;
    bus->gpio.ctx = bus;
    bus->usb_mode_switch.ops = &usb_mode_switch_ops;
    bus->usb_mode_switch.ctx = bus;

    pbus_interface_t intf;
    intf.ops = &hi3660_bus_ops;
    intf.ctx = bus;
    pbus_set_interface(&bus->pbus, &intf);

    if ((status = hi3360_add_gpios(bus)) != ZX_OK) {
        printf("hi3660_bind: hi3360_add_gpios failed!\n");;
    }

    if ((status = hi3360_add_devices(bus)) != ZX_OK) {
        printf("hi3660_bind: hi3360_add_devices failed!\n");;
    }

#if 0
    thrd_t thrd;
    thrd_create_with_name(&thrd, led_test_thread, bus, "led_test_thread");
#endif

    // must be after pbus_set_interface
    if ((status = hi3360_usb_init(bus)) != ZX_OK) {
        printf("hi3660_bind: hi3360_usb_init failed!\n");;
    }
    hi3660_usb_set_mode(bus, USB_MODE_NONE);

    return ZX_OK;

fail:
    printf("hi3660_bind failed %d\n", status);
    hi3660_release(bus);
    return status;
}

static zx_driver_ops_t hi3660_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = hi3660_bind,
};

ZIRCON_DRIVER_BEGIN(hi3660, hi3660_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_BUS),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_HI_SILICON),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_HI3660),
ZIRCON_DRIVER_END(hi3660)
