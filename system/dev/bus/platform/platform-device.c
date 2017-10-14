// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/platform-defs.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "platform-bus.h"
#include "platform-proxy.h"

static zx_status_t platform_dev_get_mmio(platform_dev_t* dev, uint32_t index,
                                         zx_handle_t* out_handle, uint32_t* out_handle_count) {
    if (index >= dev->mmio_count) {
        return ZX_ERR_INVALID_ARGS;
    }

    pbus_mmio_t* mmio = &dev->mmios[index];
    zx_status_t status = zx_vmo_create_physical(dev->bus->resource, mmio->base, mmio->length,
                                                out_handle);
    if (status != ZX_OK) {
        dprintf(ERROR, "platform_dev_map_mmio: zx_vmo_create_physical failed %d\n", status);
        return status;
    }
    *out_handle_count = 1;
    return ZX_OK;
}

static zx_status_t platform_dev_get_interrupt(platform_dev_t* dev, uint32_t index,
                                              zx_handle_t* out_handle, uint32_t* out_handle_count) {
    if (index >= dev->irq_count || !out_handle) {
        return ZX_ERR_INVALID_ARGS;
    }
    pbus_irq_t* irq = &dev->irqs[index];
    zx_status_t status = zx_interrupt_create(dev->bus->resource, irq->irq, ZX_INTERRUPT_REMAP_IRQ, out_handle);
    if (status != ZX_OK) {
        dprintf(ERROR, "platform_dev_get_interrupt: zx_interrupt_create failed %d\n", status);
        return status;
    }
    *out_handle_count = 1;
    return ZX_OK;
}

static zx_status_t platform_dev_ums_get_initial_mode(platform_dev_t* dev, usb_mode_t* out_mode) {
    platform_bus_t* bus = dev->bus;
    if (!bus->ums.ops) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    return usb_mode_switch_get_initial_mode(&bus->ums, out_mode);
}

static zx_status_t platform_dev_ums_set_mode(platform_dev_t* dev, usb_mode_t mode) {
    platform_bus_t* bus = dev->bus;
    if (!bus->ums.ops) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    return usb_mode_switch_set_mode(&bus->ums, mode);
}

static zx_status_t platform_dev_gpio_config(platform_dev_t* dev, uint32_t index,
                                            gpio_config_flags_t flags) {
    platform_bus_t* bus = dev->bus;
    if (!bus->gpio.ops) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    if (index >= dev->gpio_count) {
        return ZX_ERR_INVALID_ARGS;
    }
    index = dev->gpios[index].gpio;

    return gpio_config(&bus->gpio, index, flags);
}

static zx_status_t platform_dev_gpio_read(platform_dev_t* dev, uint32_t index, uint8_t* out_value) {
    platform_bus_t* bus = dev->bus;
    if (!bus->gpio.ops) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    if (index >= dev->gpio_count) {
        return ZX_ERR_INVALID_ARGS;
    }
    index = dev->gpios[index].gpio;

    return gpio_read(&bus->gpio, index, out_value);
}

static zx_status_t platform_dev_gpio_write(platform_dev_t* dev, uint32_t index, uint8_t value) {
    platform_bus_t* bus = dev->bus;
    if (!bus->gpio.ops) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    if (index >= dev->gpio_count) {
        return ZX_ERR_INVALID_ARGS;
    }
    index = dev->gpios[index].gpio;

    return gpio_write(&bus->gpio, index, value);
}

static zx_status_t platform_dev_rxrpc(void* ctx, zx_handle_t channel) {
    platform_dev_t* dev = ctx;
    pdev_req_t req;
    pdev_resp_t resp;
    uint32_t len = sizeof(req);

    zx_status_t status = zx_channel_read(channel, 0, &req, NULL, len, 0, &len, NULL);
    if (status != ZX_OK) {
        dprintf(ERROR, "platform_dev_rxrpc: zx_channel_read failed %d\n", status);
        return status;
    } else if (len != sizeof(req)) {
        dprintf(ERROR, "platform_dev_rxrpc: req length wrong %u\n", len);
        return ZX_ERR_INTERNAL;
    }

    resp.txid = req.txid;
    zx_handle_t handle = ZX_HANDLE_INVALID;
    uint32_t handle_count = 0;

    switch (req.op) {
    case PDEV_GET_MMIO:
        resp.status = platform_dev_get_mmio(dev, req.index, &handle, &handle_count);
        break;
    case PDEV_GET_INTERRUPT:
        resp.status = platform_dev_get_interrupt(dev, req.index, &handle, &handle_count);
        break;
    case PDEV_UMS_GET_INITIAL_MODE:
        resp.status = platform_dev_ums_get_initial_mode(dev, &resp.usb_mode);
        break;
    case PDEV_UMS_SET_MODE:
        resp.status = platform_dev_ums_set_mode(dev, req.usb_mode);
        break;
    case PDEV_GPIO_CONFIG:
        resp.status = platform_dev_gpio_config(dev, req.index, req.gpio_flags);
        break;
    case PDEV_GPIO_READ:
        resp.status = platform_dev_gpio_read(dev, req.index, &resp.gpio_value);
        break;
    case PDEV_GPIO_WRITE:
        resp.status = platform_dev_gpio_write(dev, req.index, req.gpio_value);
        break;
    default:
        dprintf(ERROR, "platform_dev_rxrpc: unknown op %u\n", req.op);
        return ZX_ERR_INTERNAL;
    }

    // set op to match request so zx_channel_write will return our response
    status = zx_channel_write(channel, 0, &resp, sizeof(resp), (handle_count == 1 ? &handle : NULL),
                              handle_count);
    if (status != ZX_OK) {
        dprintf(ERROR, "platform_dev_rxrpc: zx_channel_write failed %d\n", status);
    }
    return status;
}

void platform_dev_free(platform_dev_t* dev) {
    free(dev->mmios);
    free(dev->irqs);
    free(dev->gpios);
    free(dev);
}

static zx_protocol_device_t platform_dev_proto = {
    .version = DEVICE_OPS_VERSION,
    .rxrpc = platform_dev_rxrpc,
    // Note that we do not have a release callback here because we
    // need to support re-adding platform devices when they are reenabled.
};

zx_status_t platform_device_add(platform_bus_t* bus, const pbus_dev_t* pdev, uint32_t flags) {
    zx_status_t status = ZX_OK;

    if (flags & ~PDEV_ADD_DISABLED) {
        return ZX_ERR_INVALID_ARGS;
    }

    platform_dev_t* dev = calloc(1, sizeof(platform_dev_t));
    if (!dev) {
        return ZX_ERR_NO_MEMORY;
    }
    if (pdev->mmio_count) {
        size_t size = pdev->mmio_count * sizeof(*pdev->mmios);
        dev->mmios = malloc(size);
        if (!dev->mmios) {
            status = ZX_ERR_NO_MEMORY;
            goto fail;
        }
        memcpy(dev->mmios, pdev->mmios, size);
        dev->mmio_count = pdev->mmio_count;
    }
    if (pdev->irq_count) {
        size_t size = pdev->irq_count * sizeof(*pdev->irqs);
        dev->irqs = malloc(size);
        if (!dev->irqs) {
            status = ZX_ERR_NO_MEMORY;
            goto fail;
        }
        memcpy(dev->irqs, pdev->irqs, size);
        dev->irq_count = pdev->irq_count;
    }
    if (pdev->gpio_count) {
        size_t size = pdev->gpio_count * sizeof(*pdev->gpios);
        dev->gpios = malloc(size);
        if (!dev->gpios) {
            status = ZX_ERR_NO_MEMORY;
            goto fail;
        }
        memcpy(dev->gpios, pdev->gpios, size);
        dev->gpio_count = pdev->gpio_count;
    }

    dev->bus = bus;
    strlcpy(dev->name, pdev->name, sizeof(dev->name));
    dev->vid = pdev->vid;
    dev->pid = pdev->pid;
    dev->did = pdev->did;

    list_add_tail(&bus->devices, &dev->node);

    if ((flags & PDEV_ADD_DISABLED) == 0) {
        status = platform_device_enable(dev, true);
    }

fail:
    if (status != ZX_OK) {
        platform_dev_free(dev);
    }

    return status;
}

zx_status_t platform_device_enable(platform_dev_t* dev, bool enable) {
    zx_status_t status = ZX_OK;

    if (enable && !dev->enabled) {
        zx_device_prop_t props[] = {
            {BIND_PLATFORM_DEV_VID, 0, dev->vid},
            {BIND_PLATFORM_DEV_PID, 0, dev->pid},
            {BIND_PLATFORM_DEV_DID, 0, dev->did},
        };

        char namestr[ZX_DEVICE_NAME_MAX];
        snprintf(namestr, sizeof(namestr), "%04x:%04x:%04x", dev->vid, dev->pid, dev->did);
        char argstr[64];
        snprintf(argstr, sizeof(argstr), "pdev:%s,", namestr);

        device_add_args_t args = {
            .version = DEVICE_ADD_ARGS_VERSION,
            .name = namestr,
            .ctx = dev,
            .ops = &platform_dev_proto,
            .proto_id = ZX_PROTOCOL_PLATFORM_DEV,
            .props = props,
            .prop_count = countof(props),
            .proxy_args = argstr,
            .flags = DEVICE_ADD_MUST_ISOLATE,
        };
        // add PCI root at top level
        zx_device_t* parent = dev->bus->zxdev;
        if (dev->did == PDEV_DID_KPCI) {
            parent = device_get_parent(parent);
        }
        status = device_add(parent, &args, &dev->zxdev);
    } else if (!enable && dev->enabled) {
        device_remove(dev->zxdev);
        dev->zxdev = NULL;
    }

    if (status == ZX_OK) {
        dev->enabled = enable;
    }

    return status;
}
