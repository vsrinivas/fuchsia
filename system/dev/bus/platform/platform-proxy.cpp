// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/clk.h>
#include <ddk/protocol/usb-mode-switch.h>

#include "platform-proxy.h"

typedef struct {
    zx_device_t* zxdev;
    zx_handle_t rpc_channel;
} platform_proxy_t;

static zx_status_t platform_dev_rpc(platform_proxy_t* proxy, pdev_req_t* req, uint32_t req_length,
                                    pdev_resp_t* resp, uint32_t resp_length,
                                    zx_handle_t* in_handles, uint32_t in_handle_count,
                                    zx_handle_t* out_handles, uint32_t out_handle_count,
                                    uint32_t* out_data_received) {
    uint32_t resp_size, handle_count;

    zx_channel_call_args_t args = {
        .wr_bytes = req,
        .wr_handles = in_handles,
        .rd_bytes = resp,
        .rd_handles = out_handles,
        .wr_num_bytes = req_length,
        .wr_num_handles = in_handle_count,
        .rd_num_bytes = resp_length,
        .rd_num_handles = out_handle_count,
    };
    zx_status_t status = zx_channel_call(proxy->rpc_channel, 0, ZX_TIME_INFINITE, &args, &resp_size,
                                         &handle_count);
    if (status != ZX_OK) {
        return status;
    } else if (resp_size < sizeof(*resp)) {
        zxlogf(ERROR, "platform_dev_rpc resp_size too short: %u\n", resp_size);
        status = ZX_ERR_INTERNAL;
        goto fail;
    } else if (handle_count != out_handle_count) {
        zxlogf(ERROR, "platform_dev_rpc handle count %u expected %u\n", handle_count,
                out_handle_count);
        status = ZX_ERR_INTERNAL;
        goto fail;
    }

    status = resp->status;
    if (out_data_received) {
        *out_data_received = static_cast<uint32_t>(resp_size - sizeof(pdev_resp_t));
    }

fail:
    if (status != ZX_OK) {
        for (uint32_t i = 0; i < handle_count; i++) {
            zx_handle_close(out_handles[i]);
        }
    }
    return status;
}

static zx_status_t pdev_ums_set_mode(void* ctx, usb_mode_t mode) {
    platform_proxy_t* proxy = static_cast<platform_proxy_t*>(ctx);
    pdev_req_t req = {};
    req.op = PDEV_UMS_SET_MODE;
    req.usb_mode = mode;
    pdev_resp_t resp;

    return platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp), nullptr, 0,
                            nullptr, 0, nullptr);
}

usb_mode_switch_protocol_ops_t usb_mode_switch_ops = {
    .set_mode = pdev_ums_set_mode,
};

static zx_status_t pdev_gpio_config(void* ctx, uint32_t index, uint32_t flags) {
    platform_proxy_t* proxy = static_cast<platform_proxy_t*>(ctx);
    pdev_req_t req = {};
    req.op = PDEV_GPIO_CONFIG;
    req.index = index;
    req.gpio_flags = flags;
    pdev_resp_t resp;

    return platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp), nullptr, 0,
                            nullptr, 0, nullptr);
}

static zx_status_t pdev_gpio_set_alt_function(void* ctx, uint32_t index, uint64_t function) {
    platform_proxy_t* proxy = static_cast<platform_proxy_t*>(ctx);
    pdev_req_t req = {};
    req.op = PDEV_GPIO_SET_ALT_FUNCTION;
    req.index = index;
    req.gpio_alt_function = function;
    pdev_resp_t resp;

    return platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp), nullptr, 0,
                            nullptr, 0, nullptr);
}

static zx_status_t pdev_gpio_get_interrupt(void* ctx, uint32_t index,
                                           uint32_t flags,
                                           zx_handle_t *out_handle) {
    platform_proxy_t* proxy = static_cast<platform_proxy_t*>(ctx);
    pdev_req_t req = {};
    req.op = PDEV_GPIO_GET_INTERRUPT;
    req.index = index;
    req.flags = flags;
    pdev_resp_t resp;

    return platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp), nullptr, 0, out_handle,
                            1, nullptr);
}

static zx_status_t pdev_gpio_set_polarity(void* ctx, uint32_t index, uint32_t polarity) {
    platform_proxy_t* proxy = static_cast<platform_proxy_t*>(ctx);
    pdev_req_t req = {};
    req.op = PDEV_GPIO_SET_POLARITY;
    req.index = index;
    req.flags = polarity;
    pdev_resp_t resp;
    return platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp), nullptr, 0, nullptr, 0,
                            nullptr);
}
static zx_status_t pdev_gpio_release_interrupt(void *ctx, uint32_t index) {
    platform_proxy_t* proxy = static_cast<platform_proxy_t*>(ctx);
    pdev_req_t req = {};
    req.op = PDEV_GPIO_RELEASE_INTERRUPT;
    req.index = index;
    pdev_resp_t resp;
    return platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp), nullptr, 0, nullptr, 0,
                            nullptr);
}

static zx_status_t pdev_gpio_read(void* ctx, uint32_t index, uint8_t* out_value) {
    platform_proxy_t* proxy = static_cast<platform_proxy_t*>(ctx);
    pdev_req_t req = {};
    req.op = PDEV_GPIO_READ;
    req.index = index;
    pdev_resp_t resp;

    zx_status_t status = platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp), nullptr, 0,
                                          nullptr, 0, nullptr);
    if (status != ZX_OK) {
        return status;
    }
    *out_value = resp.gpio_value;
    return ZX_OK;
}

static zx_status_t pdev_gpio_write(void* ctx, uint32_t index, uint8_t value) {
    platform_proxy_t* proxy = static_cast<platform_proxy_t*>(ctx);
    pdev_req_t req = {};
    req.op = PDEV_GPIO_WRITE;
    req.index = index;
    req.gpio_value = value;
    pdev_resp_t resp;

    return platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp), nullptr, 0, nullptr, 0,
                            nullptr);
}

static gpio_protocol_ops_t gpio_ops = {
    .config = pdev_gpio_config,
    .set_alt_function = pdev_gpio_set_alt_function,
    .read = pdev_gpio_read,
    .write = pdev_gpio_write,
    .get_interrupt = pdev_gpio_get_interrupt,
    .release_interrupt = pdev_gpio_release_interrupt,
    .set_polarity = pdev_gpio_set_polarity,
};

static zx_status_t pdev_scpi_get_sensor_value(void* ctx, uint32_t sensor_id,
                                         uint32_t* sensor_value) {
    platform_proxy_t* proxy = static_cast<platform_proxy_t*>(ctx);
    pdev_req_t req = {};
    req.op = PDEV_SCPI_GET_SENSOR_VALUE;
    req.scpi_sensor_id = sensor_id;
    pdev_resp_t resp;

    zx_status_t status =  platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp),
                                           nullptr, 0, nullptr, 0, nullptr);
    if (status == ZX_OK) {
        *sensor_value = resp.scpi_sensor_value;
    }
    return status;
}

static zx_status_t pdev_scpi_get_sensor(void* ctx, const char* name,
                                         uint32_t* sensor_id) {
    platform_proxy_t* proxy = static_cast<platform_proxy_t*>(ctx);
    pdev_req_t req = {};
    req.op = PDEV_SCPI_GET_SENSOR;
    uint32_t max_len = sizeof(req.scpi_name);
    size_t len = strnlen(name, max_len);
    if (len == max_len) {
        return ZX_ERR_INVALID_ARGS;
    }
    memcpy(&req.scpi_name, name, len + 1);
    pdev_resp_t resp;

    zx_status_t status =  platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp),
                                           nullptr, 0, nullptr, 0, nullptr);
    if (status == ZX_OK) {
        *sensor_id = resp.scpi_sensor_id;
    }
    return status;
}

static zx_status_t pdev_scpi_get_dvfs_info(void* ctx, uint8_t power_domain,
                                           scpi_opp_t* opps) {
    platform_proxy_t* proxy = static_cast<platform_proxy_t*>(ctx);
    pdev_req_t req = {};
    req.op = PDEV_SCPI_GET_DVFS_INFO;
    req.scpi_power_domain = power_domain;
    pdev_resp_t resp;

    zx_status_t status =  platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp),
                                           nullptr, 0, nullptr, 0, nullptr);
    if (status == ZX_OK) {
        memcpy(opps, &resp.scpi_opps, sizeof(scpi_opp_t));
    }
    return status;
}

static zx_status_t pdev_scpi_get_dvfs_idx(void* ctx, uint8_t power_domain,
                                           uint16_t* idx) {
    platform_proxy_t* proxy = static_cast<platform_proxy_t*>(ctx);
    pdev_req_t req = {};
    req.op = PDEV_SCPI_GET_DVFS_IDX;
    req.scpi_power_domain = power_domain;
    pdev_resp_t resp;

    zx_status_t status =  platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp),
                                           nullptr, 0, nullptr, 0, nullptr);
    if (status == ZX_OK) {
        *idx = resp.scpi_dvfs_idx;
    }
    return status;
}

static zx_status_t pdev_scpi_set_dvfs_idx(void* ctx, uint8_t power_domain,
                                           uint16_t idx) {
    platform_proxy_t* proxy = static_cast<platform_proxy_t*>(ctx);
    pdev_req_t req = {};
    req.op = PDEV_SCPI_SET_DVFS_IDX;
    req.index = idx;
    req.scpi_power_domain = power_domain;
    pdev_resp_t resp;
    return platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp),
                            nullptr, 0, nullptr, 0, nullptr);
}

static scpi_protocol_ops_t scpi_ops = {
    .get_sensor = pdev_scpi_get_sensor,
    .get_sensor_value = pdev_scpi_get_sensor_value,
    .get_dvfs_info = pdev_scpi_get_dvfs_info,
    .get_dvfs_idx = pdev_scpi_get_dvfs_idx,
    .set_dvfs_idx = pdev_scpi_set_dvfs_idx,
};

static zx_status_t pdev_canvas_config(void* ctx, zx_handle_t vmo,
                                      size_t offset, canvas_info_t* info,
                                      uint8_t* canvas_idx) {
    platform_proxy_t* proxy = static_cast<platform_proxy_t*>(ctx);
    zx_status_t status = ZX_OK;
    pdev_resp_t resp;
    pdev_req_t req = {};
    req.op = PDEV_CANVAS_CONFIG;

    memcpy((void*)&req.canvas.info, info, sizeof(canvas_info_t));
    req.canvas.offset = offset;

    status = platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp),
                              &vmo, 1, nullptr, 0, nullptr);
    if (status == ZX_OK) {
        *canvas_idx = resp.canvas_idx;
    }
    return status;
}

static zx_status_t pdev_canvas_free(void* ctx, uint8_t canvas_idx) {
    platform_proxy_t* proxy = static_cast<platform_proxy_t*>(ctx);
    pdev_req_t req = {};
    req.op = PDEV_CANCAS_FREE;
    req.canvas_idx = canvas_idx;
    pdev_resp_t resp;

    return platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp),
                            nullptr, 0, nullptr, 0, nullptr);
}

static canvas_protocol_ops_t canvas_ops = {
    .config = pdev_canvas_config,
    .free = pdev_canvas_free,
};

static zx_status_t pdev_i2c_get_max_transfer_size(void* ctx, uint32_t index, size_t* out_size) {
    platform_proxy_t* proxy = static_cast<platform_proxy_t*>(ctx);

    pdev_req_t req = {};
    req.op = PDEV_I2C_GET_MAX_TRANSFER;
    req.index = index;
    pdev_resp_t resp;

    zx_status_t status = platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp),
                                          nullptr, 0, nullptr, 0, nullptr);
    if (status == ZX_OK) {
        *out_size = resp.i2c_max_transfer;
    }
    return status;
}

static zx_status_t pdev_i2c_transact(void* ctx, uint32_t index, const void* write_buf,
                                     size_t write_length, size_t read_length,
                                     i2c_complete_cb complete_cb, void* cookie) {
    platform_proxy_t* proxy = static_cast<platform_proxy_t*>(ctx);

    if (!read_length && !write_length) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (write_length > PDEV_I2C_MAX_TRANSFER_SIZE ||
        read_length > PDEV_I2C_MAX_TRANSFER_SIZE) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    struct {
        pdev_req_t req;
        uint8_t data[PDEV_I2C_MAX_TRANSFER_SIZE];
    } req = {
        .req = {
            .txid = 0,
            .op = PDEV_I2C_TRANSACT,
            .index = index,
            .i2c_txn = {
                .write_length = write_length,
                .read_length = read_length,
                .complete_cb = complete_cb,
                .cookie = cookie,
            },
        },
        .data = {},
    };
    struct {
        pdev_resp_t resp;
        uint8_t data[PDEV_I2C_MAX_TRANSFER_SIZE];
    } resp;

    if (write_length) {
        memcpy(req.data, write_buf, write_length);
    }
    uint32_t data_received;
    zx_status_t status = platform_dev_rpc(proxy, &req.req,
                                          static_cast<uint32_t>(sizeof(req.req) + write_length),
                                          &resp.resp, sizeof(resp),
                                          nullptr, 0, nullptr, 0, &data_received);
    if (status != ZX_OK) {
        return status;
    }

    // TODO(voydanoff) This proxying code actually implements i2c_transact synchronously
    // due to the fact that it is unsafe to respond asynchronously on the devmgr rxrpc channel.
    // In the future we may want to redo the plumbing to allow this to be truly asynchronous.
    if (data_received != read_length) {
        status = ZX_ERR_INTERNAL;
    } else {
        status = resp.resp.status;
    }
    if (complete_cb) {
        complete_cb(status, resp.data, resp.resp.i2c_txn.cookie);
    }

    return ZX_OK;
}

static void pdev_i2c_channel_release(void* ctx) {
    free(ctx);
}

static i2c_protocol_ops_t i2c_ops = {
    .transact = pdev_i2c_transact,
    .get_max_transfer_size = pdev_i2c_get_max_transfer_size,
};

static zx_status_t pdev_clk_enable(void* ctx, uint32_t index) {
    platform_proxy_t* proxy = static_cast<platform_proxy_t*>(ctx);
    pdev_req_t req = {};
    req.op = PDEV_CLK_ENABLE;
    req.index = index;
    pdev_resp_t resp;

    return platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp),
                            nullptr, 0, nullptr, 0, nullptr);
}

static zx_status_t pdev_clk_disable(void* ctx, uint32_t index) {
    platform_proxy_t* proxy = static_cast<platform_proxy_t*>(ctx);
    pdev_req_t req = {};
    req.op = PDEV_CLK_DISABLE;
    req.index = index;
    pdev_resp_t resp;

    return platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp),
                            nullptr, 0, nullptr, 0, nullptr);
}

static clk_protocol_ops_t clk_ops = {
    .enable = pdev_clk_enable,
    .disable = pdev_clk_disable,
};

static zx_status_t platform_dev_map_mmio(void* ctx, uint32_t index, uint32_t cache_policy,
                                         void** vaddr, size_t* size, zx_paddr_t* out_paddr,
                                         zx_handle_t* out_handle) {
    platform_proxy_t* proxy = static_cast<platform_proxy_t*>(ctx);
    pdev_req_t req = {};
    req.op = PDEV_GET_MMIO;
    req.index = index;
    pdev_resp_t resp;
    zx_handle_t vmo_handle;

    zx_status_t status = platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp),
                                          nullptr, 0, &vmo_handle, 1, nullptr);
    if (status != ZX_OK) {
        return status;
    }

    size_t vmo_size;
    status = zx_vmo_get_size(vmo_handle, &vmo_size);
    if (status != ZX_OK) {
        zxlogf(ERROR, "platform_dev_map_mmio: zx_vmo_get_size failed %d\n", status);
        goto fail;
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

    *size = resp.mmio.length;
    *out_handle = vmo_handle;
    if (out_paddr) {
        *out_paddr = resp.mmio.paddr;
    }
    *vaddr = (void *)(virt + resp.mmio.offset);
    return ZX_OK;

fail:
    zx_handle_close(vmo_handle);
    return status;
}

static zx_status_t platform_dev_map_interrupt(void* ctx, uint32_t index,
                                              uint32_t flags, zx_handle_t* out_handle) {
    platform_proxy_t* proxy = static_cast<platform_proxy_t*>(ctx);
    pdev_req_t req = {};
    req.op = PDEV_GET_INTERRUPT;
    req.index = index;
    req.flags = flags;
    pdev_resp_t resp;

    return platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp),
                            nullptr, 0, out_handle, 1, nullptr);
}

static zx_status_t platform_dev_get_bti(void* ctx, uint32_t index, zx_handle_t* out_handle) {
    platform_proxy_t* proxy = static_cast<platform_proxy_t*>(ctx);
    pdev_req_t req = {};
    req.op = PDEV_GET_BTI;
    req.index = index;
    pdev_resp_t resp;

    return platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp),
                            nullptr, 0, out_handle, 1, nullptr);
}

static zx_status_t platform_dev_get_device_info(void* ctx, pdev_device_info_t* out_info) {
    platform_proxy_t* proxy = static_cast<platform_proxy_t*>(ctx);
    pdev_req_t req = {};
    req.op = PDEV_GET_DEVICE_INFO;
    pdev_resp_t resp;

    zx_status_t status = platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp),
                                          nullptr, 0, nullptr, 0, nullptr);
    if (status != ZX_OK) {
        return status;
    }
    memcpy(out_info, &resp.info, sizeof(*out_info));
    return ZX_OK;
}

static platform_device_protocol_ops_t platform_dev_proto_ops = {
    .map_mmio = platform_dev_map_mmio,
    .map_interrupt = platform_dev_map_interrupt,
    .get_bti = platform_dev_get_bti,
    .get_device_info = platform_dev_get_device_info,
};

static zx_status_t platform_dev_get_protocol(void* ctx, uint32_t proto_id, void* out) {
    switch (proto_id) {
    case ZX_PROTOCOL_PLATFORM_DEV: {
        platform_device_protocol_t* proto = static_cast<platform_device_protocol_t*>(out);
        proto->ctx = ctx;
        proto->ops = &platform_dev_proto_ops;
        return ZX_OK;
    }
    case ZX_PROTOCOL_USB_MODE_SWITCH: {
        usb_mode_switch_protocol_t* proto = static_cast<usb_mode_switch_protocol_t*>(out);
        proto->ctx = ctx;
        proto->ops = &usb_mode_switch_ops;
        return ZX_OK;
    }
    case ZX_PROTOCOL_GPIO: {
        gpio_protocol_t* proto = static_cast<gpio_protocol_t*>(out);
        proto->ctx = ctx;
        proto->ops = &gpio_ops;
        return ZX_OK;
    }
    case ZX_PROTOCOL_I2C: {
        i2c_protocol_t* proto = static_cast<i2c_protocol_t*>(out);
        proto->ctx = ctx;
        proto->ops = &i2c_ops;
        return ZX_OK;
    }
    case ZX_PROTOCOL_CLK: {
        clk_protocol_t* proto = static_cast<clk_protocol_t*>(out);
        proto->ctx = ctx;
        proto->ops = &clk_ops;
        return ZX_OK;
    }
    case ZX_PROTOCOL_SCPI: {
        scpi_protocol_t* proto = static_cast<scpi_protocol_t*>(out);
        proto->ctx = ctx;
        proto->ops = &scpi_ops;
        return ZX_OK;
    }
    case ZX_PROTOCOL_CANVAS: {
        canvas_protocol_t* proto = static_cast<canvas_protocol_t*>(out);
        proto->ctx = ctx;
        proto->ops = &canvas_ops;
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static void platform_dev_release(void* ctx) {
    platform_proxy_t* proxy = static_cast<platform_proxy_t*>(ctx);

    zx_handle_close(proxy->rpc_channel);
    free(proxy);
}

static zx_protocol_device_t platform_dev_proto = {
    .version = DEVICE_OPS_VERSION,
    .get_protocol = platform_dev_get_protocol,
    .open = nullptr,
    .open_at = nullptr,
    .close = nullptr,
    .unbind = nullptr,
    .release = platform_dev_release,
    .read = nullptr,
    .write = nullptr,
    .get_size = nullptr,
    .ioctl = nullptr,
    .suspend = nullptr,
    .resume = nullptr,
    .rxrpc = nullptr,
    .message = nullptr,
};

zx_status_t platform_proxy_create(void* ctx, zx_device_t* parent, const char* name,
                                  const char* args, zx_handle_t rpc_channel) {
    platform_proxy_t* proxy = static_cast<platform_proxy_t*>(calloc(1, sizeof(platform_proxy_t)));
    if (!proxy) {
        return ZX_ERR_NO_MEMORY;
    }
    proxy->rpc_channel = rpc_channel;

    device_add_args_t add_args = {};
    add_args.version = DEVICE_ADD_ARGS_VERSION;
    add_args.name = name;
    add_args.ctx = proxy;
    add_args.ops = &platform_dev_proto;
    add_args.proto_id = ZX_PROTOCOL_PLATFORM_DEV;
    add_args.proto_ops = &platform_dev_proto_ops;

    zx_status_t status = device_add(parent, &add_args, &proxy->zxdev);
    if (status != ZX_OK) {
        zx_handle_close(rpc_channel);
        free(proxy);
    }

    return status;
}
