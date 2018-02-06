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

static i2c_txn_t* get_i2c_txn(platform_bus_t* bus, pdev_req_t* req, zx_handle_t channel) {
    mtx_lock(&bus->i2c_txn_lock);
    i2c_txn_t* txn = list_remove_head_type(&bus->i2c_txns, i2c_txn_t, node);
    mtx_unlock(&bus->i2c_txn_lock);

    if (!txn) {
        txn = malloc(sizeof(i2c_txn_t));
        if (!txn) {
            return NULL;
        }
    }

    txn->bus = bus;
    txn->txid = req->txid;
    txn->complete_cb = req->i2c.txn_ctx.complete_cb;
    txn->cookie = req->i2c.txn_ctx.cookie;
    txn->channel = channel;

    return txn;
}

static void put_i2c_txn(i2c_txn_t* txn) {
    platform_bus_t* bus = txn->bus;

    mtx_lock(&bus->i2c_txn_lock);
    list_add_tail(&bus->i2c_txns, &txn->node);
    mtx_unlock(&bus->i2c_txn_lock);
}

static zx_status_t platform_dev_map_mmio(void* ctx, uint32_t index, uint32_t cache_policy,
                                         void** vaddr, size_t* size, zx_handle_t* out_handle) {
    platform_dev_t* dev = ctx;

    if (index >= dev->mmio_count) {
        return ZX_ERR_INVALID_ARGS;
    }

    pbus_mmio_t* mmio = &dev->mmios[index];
    zx_paddr_t vmo_base = ROUNDDOWN(mmio->base, PAGE_SIZE);
    size_t vmo_size = ROUNDUP(mmio->base + mmio->length - vmo_base, PAGE_SIZE);
    zx_handle_t vmo_handle;
    zx_status_t status = zx_vmo_create_physical(dev->bus->resource, vmo_base, vmo_size,
                                                &vmo_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "platform_dev_map_mmio: zx_vmo_create_physical failed %d\n", status);
        return status;
    }

    status = zx_vmo_set_cache_policy(vmo_handle, cache_policy);
    if (status != ZX_OK) {
        zxlogf(ERROR, "platform_dev_map_mmio: zx_vmo_set_cache_policy failed %d\n", status);
        goto fail;
    }

    uintptr_t virt;
    status = zx_vmar_map(zx_vmar_root_self(), 0, vmo_handle, 0, vmo_size,
                         ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_MAP_RANGE,
                         &virt);
    if (status != ZX_OK) {
        zxlogf(ERROR, "platform_dev_map_mmio: zx_vmar_map failed %d\n", status);
        goto fail;
    }

    *size = mmio->length;
    *out_handle = vmo_handle;
    *vaddr = (void *)(virt + (mmio->base - vmo_base));
    return ZX_OK;

fail:
    zx_handle_close(vmo_handle);
    return status;
}

static zx_status_t platform_dev_map_interrupt(void* ctx, uint32_t index, zx_handle_t* out_handle) {
    platform_dev_t* dev = ctx;

    if (index >= dev->irq_count || !out_handle) {
        return ZX_ERR_INVALID_ARGS;
    }
    pbus_irq_t* irq = &dev->irqs[index];
    zx_status_t status = zx_interrupt_create(dev->bus->resource, 0, out_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "platform_dev_map_interrupt: zx_interrupt_create failed %d\n", status);
        return status;
    }
    status = zx_interrupt_bind(*out_handle, 0, dev->bus->resource, irq->irq, irq->mode);
    if (status != ZX_OK) {
        zxlogf(ERROR, "platform_dev_map_interrupt: zx_interrupt_bind failed %d\n", status);
        zx_handle_close(*out_handle);
    }
    return status;
}

static zx_status_t platform_dev_alloc_contig_vmo(void* ctx, size_t size, uint32_t align_log2,
                                                 zx_handle_t* out_handle) {
    platform_dev_t* dev = ctx;
    zx_status_t status = zx_vmo_create_contiguous(dev->bus->resource, size, align_log2, out_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "platform_dev_alloc_contig_vmo: zx_vmo_create_contiguous failed %d\n",
               status);
    }
    return status;
}

static zx_status_t platform_dev_map_contig_vmo(void* ctx, size_t size, uint32_t align_log2,
                                               uint32_t map_flags, void** out_vaddr,
                                               zx_paddr_t* out_paddr, zx_handle_t* out_handle) {
    zx_handle_t handle;
    zx_status_t status = platform_dev_alloc_contig_vmo(ctx, size, align_log2, &handle);
    if (status != ZX_OK) {
        return status;
    }

    status = zx_vmo_op_range(handle, ZX_VMO_OP_LOOKUP, 0, PAGE_SIZE, out_paddr, sizeof(*out_paddr));
    if (status != ZX_OK) {
        zxlogf(ERROR, "platform_dev_map_contig_vmo: zx_vmo_op_range failed %d\n", status);
        zx_handle_close(handle);
        return status;
    }

    uintptr_t addr;
    status = zx_vmar_map(zx_vmar_root_self(), 0, handle, 0, size, map_flags, &addr);
    if (status != ZX_OK) {
        zxlogf(ERROR, "platform_dev_map_contig_vmo: zx_vmar_map failed %d\n", status);
        zx_handle_close(handle);
        return status;
    }

    *out_vaddr = (void *)addr;
    *out_handle = handle;
    return ZX_OK;
}

static zx_status_t platform_dev_get_device_info(void* ctx, pdev_device_info_t* out_info) {
    platform_dev_t* dev = ctx;

    memset(out_info, 0, sizeof(*out_info));
    out_info->vid = dev->vid;
    out_info->pid = dev->pid;
    out_info->did = dev->did;
    out_info->mmio_count = dev->mmio_count;
    out_info->irq_count = dev->irq_count;
    out_info->gpio_count = dev->gpio_count;
    out_info->i2c_channel_count = dev->i2c_channel_count;

    return ZX_OK;
}

static platform_device_protocol_ops_t platform_dev_proto_ops = {
    .map_mmio = platform_dev_map_mmio,
    .map_interrupt = platform_dev_map_interrupt,
    .alloc_contig_vmo = platform_dev_alloc_contig_vmo,
    .map_contig_vmo = platform_dev_map_contig_vmo,
    .get_device_info = platform_dev_get_device_info,
};

static zx_status_t pdev_rpc_get_mmio(platform_dev_t* dev, uint32_t index, zx_off_t* out_offset,
                                     size_t *out_length, zx_handle_t* out_handle,
                                     uint32_t* out_handle_count) {
    if (index >= dev->mmio_count) {
        return ZX_ERR_INVALID_ARGS;
    }

    pbus_mmio_t* mmio = &dev->mmios[index];
    zx_paddr_t vmo_base = ROUNDDOWN(mmio->base, PAGE_SIZE);
    size_t vmo_size = ROUNDUP(mmio->base + mmio->length - vmo_base, PAGE_SIZE);
    zx_status_t status = zx_vmo_create_physical(dev->bus->resource, vmo_base, vmo_size,
                                                out_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "pdev_rpc_get_mmio: zx_vmo_create_physical failed %d\n", status);
        return status;
    }
    *out_offset = mmio->base - vmo_base;
    *out_length = mmio->length;
    *out_handle_count = 1;
    return ZX_OK;
}

static zx_status_t pdev_rpc_get_interrupt(platform_dev_t* dev, uint32_t index,
                                          zx_handle_t* out_handle, uint32_t* out_handle_count) {

    zx_status_t status = platform_dev_map_interrupt(dev, index, out_handle);
    if (status == ZX_OK) {
        *out_handle_count = 1;
    }
    return status;
}

static zx_status_t pdev_rpc_alloc_contig_vmo(platform_dev_t* dev, size_t size,
                                             uint32_t align_log2, zx_handle_t* out_handle,
                                             uint32_t* out_handle_count) {
    zx_status_t status = platform_dev_alloc_contig_vmo(dev, size, align_log2, out_handle);
    if (status == ZX_OK) {
        *out_handle_count = 1;
    }
    return status;
}

static zx_status_t pdev_rpc_ums_get_initial_mode(platform_dev_t* dev, usb_mode_t* out_mode) {
    platform_bus_t* bus = dev->bus;
    if (!bus->ums.ops) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    return usb_mode_switch_get_initial_mode(&bus->ums, out_mode);
}

static zx_status_t pdev_rpc_ums_set_mode(platform_dev_t* dev, usb_mode_t mode) {
    platform_bus_t* bus = dev->bus;
    if (!bus->ums.ops) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    return usb_mode_switch_set_mode(&bus->ums, mode);
}

static zx_status_t pdev_rpc_gpio_config(platform_dev_t* dev, uint32_t index,
                                        uint32_t flags) {
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

static zx_status_t pdev_rpc_gpio_set_alt_function(platform_dev_t* dev, uint32_t index,
                                                  uint32_t function) {
    platform_bus_t* bus = dev->bus;
    if (!bus->gpio.ops) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    if (index >= dev->gpio_count) {
        return ZX_ERR_INVALID_ARGS;
    }
    index = dev->gpios[index].gpio;

    return gpio_set_alt_function(&bus->gpio, index, function);
}

static zx_status_t pdev_rpc_gpio_read(platform_dev_t* dev, uint32_t index, uint8_t* out_value) {
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

static zx_status_t pdev_rpc_gpio_write(platform_dev_t* dev, uint32_t index, uint8_t value) {
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

static zx_status_t pdev_rpc_i2c_get_channel(platform_dev_t* dev, uint32_t index,
                                            pdev_i2c_resp_t* resp) {
    platform_bus_t* bus = dev->bus;
    if (!bus->i2c.ops) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    if (index >= dev->i2c_channel_count) {
        return ZX_ERR_INVALID_ARGS;
    }
    pbus_i2c_channel_t* pdev_channel = &dev->i2c_channels[index];

    i2c_channel_t* channel = calloc(1, sizeof(i2c_channel_t));
    if (!channel) {
        return ZX_ERR_NO_MEMORY;
    }
    zx_status_t status = i2c_get_channel_by_address(&bus->i2c, pdev_channel->bus_id,
                                                    pdev_channel->address, channel);
    if (status == ZX_OK) {
        resp->server_ctx = channel;
    } else {
        free(channel);
        return status;
    }
    status = i2c_get_max_transfer_size(channel, &resp->max_transfer_size);
    if (status != ZX_OK) {
        i2c_channel_release(channel);
        free(channel);
        return status;
    }

    return ZX_OK;
}

static void platform_i2c_complete(zx_status_t status, const uint8_t* data, size_t actual,
                                  void* cookie) {
    i2c_txn_t* txn = cookie;

    if (actual > PDEV_I2C_MAX_TRANSFER_SIZE) {
        status = ZX_ERR_BUFFER_TOO_SMALL;
    }

    struct {
        pdev_resp_t resp;
        uint8_t data[PDEV_I2C_MAX_TRANSFER_SIZE];
    } resp = {
        .resp = {
            .txid = txn->txid,
            .status = status,
            .i2c = {
                .txn_ctx = {
                    .complete_cb = txn->complete_cb,
                    .cookie = txn->cookie,
                },
            },
        },
    };

    if (status == ZX_OK) {
        memcpy(resp.data, data, actual);
    }

    status = zx_channel_write(txn->channel, 0, &resp, sizeof(resp.resp) + actual, NULL, 0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "platform_i2c_read_complete: zx_channel_write failed %d\n", status);
    }

    put_i2c_txn(txn);
}

static zx_status_t pdev_rpc_i2c_transact(platform_dev_t* dev, pdev_req_t* req, uint8_t* data,
                                        zx_handle_t channel) {
    // TODO(voydanoff) Do not rely on client passing back a pointer to us.
    // We need a safer solution for this.
    i2c_channel_t* i2c_channel = (i2c_channel_t *)req->i2c.server_ctx;
    i2c_txn_t* txn = get_i2c_txn(dev->bus, req, channel);
    if (!txn) {
        return ZX_ERR_NO_MEMORY;
    }

    return i2c_transact(i2c_channel, data, req->i2c.txn_ctx.write_length,
                        req->i2c.txn_ctx.read_length, platform_i2c_complete, txn);
}

static zx_status_t pdev_rpc_i2c_get_bitrate(platform_dev_t* dev, pdev_i2c_req_t* req,
                                            pdev_i2c_resp_t* resp) {
    i2c_channel_t* channel = (i2c_channel_t *)req->server_ctx;
    return i2c_set_bitrate(channel, req->bitrate);
}

static void pdev_rpc_i2c_channel_release(platform_dev_t* dev, pdev_i2c_req_t* req) {
    i2c_channel_t* channel = (i2c_channel_t *)req->server_ctx;
    return i2c_channel_release(channel);
}

static zx_status_t platform_dev_rxrpc(void* ctx, zx_handle_t channel) {
    if (channel == ZX_HANDLE_INVALID) {
        // proxy device has connected
        return ZX_OK;
    }

    platform_dev_t* dev = ctx;
    struct {
        pdev_req_t req;
        uint8_t data[PDEV_I2C_MAX_TRANSFER_SIZE];
    } req_data;
    pdev_req_t* req = &req_data.req;
    pdev_resp_t resp;
    uint32_t len = sizeof(req_data);

    zx_status_t status = zx_channel_read(channel, 0, &req_data, NULL, len, 0, &len, NULL);
    if (status != ZX_OK) {
        zxlogf(ERROR, "platform_dev_rxrpc: zx_channel_read failed %d\n", status);
        return status;
    }

    resp.txid = req->txid;
    zx_handle_t handle = ZX_HANDLE_INVALID;
    uint32_t handle_count = 0;

    switch (req->op) {
    case PDEV_GET_MMIO:
        resp.status = pdev_rpc_get_mmio(dev, req->index, &resp.mmio.offset, &resp.mmio.length,
                                            &handle, &handle_count);
        break;
    case PDEV_GET_INTERRUPT:
        resp.status = pdev_rpc_get_interrupt(dev, req->index, &handle, &handle_count);
        break;
    case PDEV_ALLOC_CONTIG_VMO:
        resp.status = pdev_rpc_alloc_contig_vmo(dev, req->contig_vmo.size,
                                                    req->contig_vmo.align_log2,
                                                    &handle, &handle_count);
        break;
    case PDEV_GET_DEVICE_INFO:
         resp.status = platform_dev_get_device_info(dev, &resp.info);
        break;
    case PDEV_UMS_GET_INITIAL_MODE:
        resp.status = pdev_rpc_ums_get_initial_mode(dev, &resp.usb_mode);
        break;
    case PDEV_UMS_SET_MODE:
        resp.status = pdev_rpc_ums_set_mode(dev, req->usb_mode);
        break;
    case PDEV_GPIO_CONFIG:
        resp.status = pdev_rpc_gpio_config(dev, req->index, req->gpio_flags);
        break;
    case PDEV_GPIO_SET_ALT_FUNCTION:
        resp.status = pdev_rpc_gpio_set_alt_function(dev, req->index, req->gpio_alt_function);
        break;
    case PDEV_GPIO_READ:
        resp.status = pdev_rpc_gpio_read(dev, req->index, &resp.gpio_value);
        break;
    case PDEV_GPIO_WRITE:
        resp.status = pdev_rpc_gpio_write(dev, req->index, req->gpio_value);
        break;
    case PDEV_I2C_GET_CHANNEL:
        resp.status = pdev_rpc_i2c_get_channel(dev, req->index, &resp.i2c);
        break;
    case PDEV_I2C_TRANSACT:
        resp.status = pdev_rpc_i2c_transact(dev, req, req_data.data, channel);
        if (resp.status == ZX_OK) {
            // If platform_i2c_transact succeeds, we return immmediately instead of calling
            // zx_channel_write below. Instead we will respond in platform_i2c_complete().
            return ZX_OK;
        }
        break;
    case PDEV_I2C_SET_BITRATE:
        resp.status = pdev_rpc_i2c_get_bitrate(dev, &req->i2c, &resp.i2c);
        break;
    case PDEV_I2C_CHANNEL_RELEASE:
        pdev_rpc_i2c_channel_release(dev, &req->i2c);
        break;
    default:
        zxlogf(ERROR, "platform_dev_rxrpc: unknown op %u\n", req->op);
        return ZX_ERR_INTERNAL;
    }

    // set op to match request so zx_channel_write will return our response
    status = zx_channel_write(channel, 0, &resp, sizeof(resp), (handle_count == 1 ? &handle : NULL),
                              handle_count);
    if (status != ZX_OK) {
        zxlogf(ERROR, "platform_dev_rxrpc: zx_channel_write failed %d\n", status);
    }
    return status;
}

static zx_status_t platform_dev_get_protocol(void* ctx, uint32_t proto_id, void* protocol) {
    platform_dev_t* dev = ctx;

    if (proto_id == ZX_PROTOCOL_PLATFORM_DEV) {
        platform_device_protocol_t* proto = protocol;
        proto->ops = &platform_dev_proto_ops;
        proto->ctx = dev;
        return ZX_OK;
    } else {
        return platform_bus_get_protocol(dev->bus, proto_id, protocol);
    }
}

void platform_dev_free(platform_dev_t* dev) {
    free(dev->mmios);
    free(dev->irqs);
    free(dev->gpios);
    free(dev->i2c_channels);
    free(dev);
}

static zx_protocol_device_t platform_dev_proto = {
    .version = DEVICE_OPS_VERSION,
    .rxrpc = platform_dev_rxrpc,
    .get_protocol = platform_dev_get_protocol,
    // Note that we do not have a release callback here because we
    // need to support re-adding platform devices when they are reenabled.
};

zx_status_t platform_device_add(platform_bus_t* bus, const pbus_dev_t* pdev, uint32_t flags) {
    zx_status_t status = ZX_OK;

    if (flags & ~(PDEV_ADD_DISABLED | PDEV_ADD_PBUS_DEVHOST)) {
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
    if (pdev->i2c_channel_count) {
        size_t size = pdev->i2c_channel_count * sizeof(*pdev->i2c_channels);
        dev->i2c_channels = malloc(size);
        if (!dev->i2c_channels) {
            status = ZX_ERR_NO_MEMORY;
            goto fail;
        }
        memcpy(dev->i2c_channels, pdev->i2c_channels, size);
        dev->i2c_channel_count = pdev->i2c_channel_count;
    }

    dev->bus = bus;
    dev->flags = flags;
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
        if (dev->vid == PDEV_VID_GENERIC && dev->pid == PDEV_PID_GENERIC &&
            dev->did == PDEV_DID_KPCI) {
            strlcpy(namestr, "pci", sizeof(namestr));
        } else {

            snprintf(namestr, sizeof(namestr), "%02x:%02x:%01x", dev->vid, dev->pid, dev->did);
        }
        char argstr[64];
        snprintf(argstr, sizeof(argstr), "pdev:%s,", namestr);
        bool new_devhost = !(dev->flags & PDEV_ADD_PBUS_DEVHOST);
        device_add_args_t args = {
            .version = DEVICE_ADD_ARGS_VERSION,
            .name = namestr,
            .ctx = dev,
            .ops = &platform_dev_proto,
            .proto_id = ZX_PROTOCOL_PLATFORM_DEV,
            .props = props,
            .prop_count = countof(props),
            .proxy_args = (new_devhost ? argstr : NULL),
            .flags = (new_devhost ? DEVICE_ADD_MUST_ISOLATE : 0),
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
