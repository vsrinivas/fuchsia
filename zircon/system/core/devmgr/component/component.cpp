// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "component.h"

#include <ddk/debug.h>
#include <fbl/algorithm.h>

#include <memory>

#include "proxy-protocol.h"

namespace component {

Component::Component(zx_device_t* parent)
        : ComponentBase(parent) {

    // These protocols are all optional, so no error checking is necessary here.
    device_get_protocol(parent, ZX_PROTOCOL_AMLOGIC_CANVAS, &canvas_);
    device_get_protocol(parent, ZX_PROTOCOL_CLOCK, &clock_);
    device_get_protocol(parent, ZX_PROTOCOL_ETH_BOARD, &eth_board_);
    device_get_protocol(parent, ZX_PROTOCOL_GPIO, &gpio_);
    device_get_protocol(parent, ZX_PROTOCOL_I2C, &i2c_);
    device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev_);
    device_get_protocol(parent, ZX_PROTOCOL_POWER, &power_);
    device_get_protocol(parent, ZX_PROTOCOL_SYSMEM, &sysmem_);
    device_get_protocol(parent, ZX_PROTOCOL_USB_MODE_SWITCH, &ums_);
}

zx_status_t Component::Bind(void* ctx, zx_device_t* parent) {
    auto dev = std::make_unique<Component>(parent);
    // The thing before the comma will become the process name, if a new process
    // is created
    const char* proxy_args = "composite-device,";
    auto status = dev->DdkAdd("component", DEVICE_ADD_NON_BINDABLE | DEVICE_ADD_MUST_ISOLATE,
                              nullptr /* props */, 0 /* prop count */, 0 /* proto id */,
                              proxy_args);
    if (status == ZX_OK) {
        // devmgr owns the memory now
        __UNUSED auto ptr = dev.release();
    }
    return status;
}

zx_status_t Component::RpcCanvas(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                                 uint32_t* out_resp_size, const zx_handle_t* req_handles,
                                 uint32_t req_handle_count, zx_handle_t* resp_handles,
                                 uint32_t* resp_handle_count) {
   if (canvas_.ops == nullptr) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    auto* req = reinterpret_cast<const AmlogicCanvasProxyRequest*>(req_buf);
    if (req_size < sizeof(*req)) {
        zxlogf(ERROR, "%s received %u, expecting %zu\n", __func__, req_size, sizeof(*req));
        return ZX_ERR_INTERNAL;
    }
    auto* resp = reinterpret_cast<AmlogicCanvasProxyResponse*>(resp_buf);
    *out_resp_size = sizeof(*resp);

    switch (req->op) {
    case AmlogicCanvasOp::CONFIG:
        if (req_handle_count != 1) {
            zxlogf(ERROR, "%s received %u handles, expecting 1\n", __func__, req_handle_count);
            return ZX_ERR_INTERNAL;
        }
        return amlogic_canvas_config(&canvas_, req_handles[0], req->offset, &req->info,
                                       &resp->canvas_idx);
    case AmlogicCanvasOp::FREE:
        if (req_handle_count != 0) {
            zxlogf(ERROR, "%s received %u handles, expecting 0\n", __func__, req_handle_count);
            return ZX_ERR_INTERNAL;
        }
        return amlogic_canvas_free(&canvas_, req->canvas_idx);
    default:
        zxlogf(ERROR, "%s: unknown clk op %u\n", __func__, static_cast<uint32_t>(req->op));
        return ZX_ERR_INTERNAL;
    }
}

zx_status_t Component::RpcClock(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                                uint32_t* out_resp_size, const zx_handle_t* req_handles,
                                uint32_t req_handle_count, zx_handle_t* resp_handles,
                                uint32_t* resp_handle_count) {
   if (clock_.ops == nullptr) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    auto* req = reinterpret_cast<const ClockProxyRequest*>(req_buf);
    if (req_size < sizeof(*req)) {
        zxlogf(ERROR, "%s received %u, expecting %zu\n", __func__, req_size, sizeof(*req));
        return ZX_ERR_INTERNAL;
    }
    auto* resp = reinterpret_cast<ProxyResponse*>(resp_buf);
    *out_resp_size = sizeof(*resp);

    switch (req->op) {
    case ClockOp::ENABLE:
        return clock_enable(&clock_, req->index);
    case ClockOp::DISABLE:
        return clock_disable(&clock_, req->index);
    default:
        zxlogf(ERROR, "%s: unknown clk op %u\n", __func__, static_cast<uint32_t>(req->op));
        return ZX_ERR_INTERNAL;
    }
}

zx_status_t Component::RpcEthBoard(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                                   uint32_t* out_resp_size, const zx_handle_t* req_handles,
                                   uint32_t req_handle_count, zx_handle_t* resp_handles,
                                   uint32_t* resp_handle_count) {
    if (eth_board_.ops == nullptr) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    auto* req = reinterpret_cast<const EthBoardProxyRequest*>(req_buf);
    if (req_size < sizeof(*req)) {
        zxlogf(ERROR, "%s received %u, expecting %zu\n", __func__, req_size, sizeof(*req));
        return ZX_ERR_INTERNAL;
    }
    auto* resp = reinterpret_cast<ProxyResponse*>(resp_buf);
    *out_resp_size = sizeof(*resp);

    switch (req->op) {
    case EthBoardOp::RESET_PHY:
        return eth_board_reset_phy(&eth_board_);
    default:
        zxlogf(ERROR, "%s: unknown ETH_BOARD op %u\n", __func__, static_cast<uint32_t>(req->op));
        return ZX_ERR_INTERNAL;
    }
}

zx_status_t Component::RpcGpio(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                               uint32_t* out_resp_size, const zx_handle_t* req_handles,
                               uint32_t req_handle_count, zx_handle_t* resp_handles,
                               uint32_t* resp_handle_count) {
    if (gpio_.ops == nullptr) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    auto* req = reinterpret_cast<const GpioProxyRequest*>(req_buf);
    if (req_size < sizeof(*req)) {
        zxlogf(ERROR, "%s received %u, expecting %zu\n", __func__, req_size, sizeof(*req));
        return ZX_ERR_INTERNAL;
    }
    auto* resp = reinterpret_cast<GpioProxyResponse*>(resp_buf);
    *out_resp_size = sizeof(*resp);

    switch (req->op) {
    case GpioOp::CONFIG_IN:
        return gpio_config_in(&gpio_, req->flags);
    case GpioOp::CONFIG_OUT:
        return gpio_config_out(&gpio_, req->value);
    case GpioOp::SET_ALT_FUNCTION:
        return gpio_set_alt_function(&gpio_, req->alt_function);
    case GpioOp::READ:
        return gpio_read(&gpio_, &resp->value);
    case GpioOp::WRITE:
        return gpio_write(&gpio_, req->value);
    case GpioOp::GET_INTERRUPT: {
        auto status = gpio_get_interrupt(&gpio_, req->flags, &resp_handles[0]);
        if (status == ZX_OK) {
            *resp_handle_count = 1;
        }
        return status;
    }
    case GpioOp::RELEASE_INTERRUPT:
        return gpio_release_interrupt(&gpio_);
    case GpioOp::SET_POLARITY:
        return gpio_set_polarity(&gpio_, req->polarity);
    default:
        zxlogf(ERROR, "%s: unknown GPIO op %u\n", __func__, static_cast<uint32_t>(req->op));
        return ZX_ERR_INTERNAL;
    }
}

void Component::I2cTransactCallback(void* cookie, zx_status_t status, const i2c_op_t* op_list,
                                    size_t op_count) {
    auto* ctx = static_cast<I2cTransactContext*>(cookie);
    ctx->result = status;
    if (status == ZX_OK && ctx->read_buf && ctx->read_length) {
        memcpy(ctx->read_buf, op_list[0].data_buffer, ctx->read_length);
    }

    sync_completion_signal(&ctx->completion);
}

zx_status_t Component::RpcI2c(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                              uint32_t* out_resp_size, const zx_handle_t* req_handles,
                              uint32_t req_handle_count, zx_handle_t* resp_handles,
                              uint32_t* resp_handle_count) {
    if (i2c_.ops == nullptr) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    auto* req = reinterpret_cast<const I2cProxyRequest*>(req_buf);
    if (req_size < sizeof(*req)) {
        zxlogf(ERROR, "%s received %u, expecting %zu\n", __func__, req_size, sizeof(*req));
        return ZX_ERR_INTERNAL;
    }
    auto* resp = reinterpret_cast<I2cProxyResponse*>(resp_buf);
    *out_resp_size = sizeof(*resp);

    switch (req->op) {
    case I2cOp::TRANSACT: {
        i2c_op_t i2c_ops[I2C_MAX_RW_OPS];
        auto* rpc_ops = reinterpret_cast<const I2cProxyOp*>(&req[1]);
        auto op_count = req->op_count;
        if (op_count > countof(i2c_ops)) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }
        auto* write_buf = reinterpret_cast<const uint8_t*>(&rpc_ops[op_count]);
        size_t read_length = 0;

        for (size_t i = 0; i < op_count; i++) {
            if (rpc_ops[i].is_read) {
                i2c_ops[i].data_buffer = nullptr;
                read_length += rpc_ops[i].length;
            } else {
                i2c_ops[i].data_buffer = write_buf;
                write_buf += rpc_ops[i].length;
            }
            i2c_ops[i].data_size = rpc_ops[i].length;
            i2c_ops[i].is_read = rpc_ops[i].is_read;
            i2c_ops[i].stop = rpc_ops[i].stop;
        }

        I2cTransactContext ctx = {};
        ctx.read_buf = &resp[1];
        ctx.read_length = read_length;

        i2c_transact(&i2c_, i2c_ops, op_count, I2cTransactCallback, &ctx);
        auto status = sync_completion_wait(&ctx.completion, ZX_TIME_INFINITE);
        if (status == ZX_OK) {
            status = ctx.result;
        }
        if (status == ZX_OK) {
            *out_resp_size = static_cast<uint32_t>(sizeof(*resp) + read_length);
        }
        return status;
    }
    case I2cOp::GET_MAX_TRANSFER_SIZE:
        return i2c_get_max_transfer_size(&i2c_, &resp->size);
    case I2cOp::GET_INTERRUPT: {
        auto status = i2c_get_interrupt(&i2c_, req->flags, &resp_handles[0]);
        if (status == ZX_OK) {
            *resp_handle_count = 1;
        }
        return status;
    }
    default:
        zxlogf(ERROR, "%s: unknown I2C op %u\n", __func__, static_cast<uint32_t>(req->op));
        return ZX_ERR_INTERNAL;
    }
}

zx_status_t Component::RpcPdev(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                               uint32_t* out_resp_size, const zx_handle_t* req_handles,
                               uint32_t req_handle_count, zx_handle_t* resp_handles,
                               uint32_t* resp_handle_count) {
    if (pdev_.ops == nullptr) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    auto* req = reinterpret_cast<const PdevProxyRequest*>(req_buf);
    if (req_size < sizeof(*req)) {
        zxlogf(ERROR, "%s received %u, expecting %zu\n", __func__, req_size, sizeof(*req));
        return ZX_ERR_INTERNAL;
    }
    auto* resp = reinterpret_cast<PdevProxyResponse*>(resp_buf);
    *out_resp_size = sizeof(*resp);

    switch (req->op) {
    case PdevOp::GET_MMIO: {
        pdev_mmio_t mmio;
        auto status = pdev_get_mmio(&pdev_, req->index, &mmio);
        if (status == ZX_OK) {
            resp->offset = mmio.offset;
            resp->size = mmio.size;
            resp_handles[0] = mmio.vmo;
            *resp_handle_count = 1;
        }
        return status;
    }
    case PdevOp::GET_INTERRUPT: {
        auto status = pdev_get_interrupt(&pdev_, req->index, req->flags, &resp_handles[0]);
        if (status == ZX_OK) {
            *resp_handle_count = 1;
        }
        return status;
    }
    case PdevOp::GET_BTI: {
        auto status = pdev_get_bti(&pdev_, req->index, &resp_handles[0]);
        if (status == ZX_OK) {
            *resp_handle_count = 1;
        }
        return status;
    }
    case PdevOp::GET_SMC: {
        auto status = pdev_get_smc(&pdev_, req->index, &resp_handles[0]);
        if (status == ZX_OK) {
            *resp_handle_count = 1;
        }
        return status;
    }
    case PdevOp::GET_DEVICE_INFO:
        return pdev_get_device_info(&pdev_, &resp->device_info);
    case PdevOp::GET_BOARD_INFO:
        return pdev_get_board_info(&pdev_, &resp->board_info);
    default:
        zxlogf(ERROR, "%s: unknown pdev op %u\n", __func__, static_cast<uint32_t>(req->op));
        return ZX_ERR_INTERNAL;
    }
}

zx_status_t Component::RpcPower(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                                uint32_t* out_resp_size, const zx_handle_t* req_handles,
                                uint32_t req_handle_count, zx_handle_t* resp_handles,
                                uint32_t* resp_handle_count) {
    if (power_.ops == nullptr) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    auto* req = reinterpret_cast<const PowerProxyRequest*>(req_buf);
    if (req_size < sizeof(*req)) {
        zxlogf(ERROR, "%s received %u, expecting %zu\n", __FUNCTION__, req_size, sizeof(*req));
        return ZX_ERR_INTERNAL;
    }

    auto* resp = reinterpret_cast<PowerProxyResponse*>(resp_buf);
    *out_resp_size = sizeof(*resp);
    switch (req->op) {
    case PowerOp::ENABLE:
        return power_enable_power_domain(&power_);
    case PowerOp::DISABLE:
        return power_disable_power_domain(&power_);
    case PowerOp::GET_STATUS:
        return power_get_power_domain_status(&power_, &resp->status);
    case PowerOp::GET_SUPPORTED_VOLTAGE_RANGE:
        return power_get_supported_voltage_range(&power_, &resp->min_voltage, &resp->max_voltage);
    case PowerOp::REQUEST_VOLTAGE:
        return power_request_voltage(&power_, req->set_voltage, &resp->actual_voltage);
    case PowerOp::WRITE_PMIC_CTRL_REG:
        return power_write_pmic_ctrl_reg(&power_, req->reg_addr, req->reg_value);
    case PowerOp::READ_PMIC_CTRL_REG:
        return power_read_pmic_ctrl_reg(&power_,req->reg_addr, &resp->reg_value);
    default:
        zxlogf(ERROR, "%s: unknown Power op %u\n", __func__, static_cast<uint32_t>(req->op));
        return ZX_ERR_INTERNAL;
    }
}

zx_status_t Component::RpcSysmem(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                                 uint32_t* out_resp_size, const zx_handle_t* req_handles,
                                 uint32_t req_handle_count, zx_handle_t* resp_handles,
                                 uint32_t* resp_handle_count) {
    if (sysmem_.ops == nullptr) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    auto* req = reinterpret_cast<const SysmemProxyRequest*>(req_buf);
    if (req_size < sizeof(*req)) {
        zxlogf(ERROR, "%s received %u, expecting %zu\n", __func__, req_size, sizeof(*req));
        return ZX_ERR_INTERNAL;
    }
    if (req_handle_count != 1) {
        zxlogf(ERROR, "%s received %u handles, expecting 1\n", __func__, req_handle_count);
        return ZX_ERR_INTERNAL;
    }
    *out_resp_size = sizeof(ProxyResponse);

    switch (req->op) {
    case SysmemOp::CONNECT:
        return sysmem_connect(&sysmem_, req_handles[0]);
    default:
        zxlogf(ERROR, "%s: unknown sysmem op %u\n", __func__, static_cast<uint32_t>(req->op));
        return ZX_ERR_INTERNAL;
    }
}

zx_status_t Component::RpcUms(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                              uint32_t* out_resp_size, const zx_handle_t* req_handles,
                              uint32_t req_handle_count, zx_handle_t* resp_handles,
                              uint32_t* resp_handle_count) {
    if (ums_.ops == nullptr) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    auto* req = reinterpret_cast<const UsbModeSwitchProxyRequest*>(req_buf);
    if (req_size < sizeof(*req)) {
        zxlogf(ERROR, "%s received %u, expecting %zu\n", __FUNCTION__, req_size, sizeof(*req));
        return ZX_ERR_INTERNAL;
    }

    auto* resp = reinterpret_cast<ProxyResponse*>(resp_buf);
    *out_resp_size = sizeof(*resp);
    switch (req->op) {
    case UsbModeSwitchOp::SET_MODE:
        return usb_mode_switch_set_mode(&ums_, req->mode);
    default:
        zxlogf(ERROR, "%s: unknown USB Mode Switch op %u\n", __func__,
               static_cast<uint32_t>(req->op));
        return ZX_ERR_INTERNAL;
    }
}

zx_status_t Component::DdkRxrpc(zx_handle_t raw_channel) {
    zx::unowned_channel channel(raw_channel);
    if (!channel->is_valid()) {
        // This driver is stateless, so we don't need to reset anything here
        return ZX_OK;
    }

    uint8_t req_buf[kProxyMaxTransferSize];
    uint8_t resp_buf[kProxyMaxTransferSize];
    auto* req_header = reinterpret_cast<ProxyRequest*>(&req_buf);
    auto* resp_header = reinterpret_cast<ProxyResponse*>(&resp_buf);
    uint32_t actual;
    zx_handle_t req_handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    zx_handle_t resp_handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    uint32_t req_handle_count;
    uint32_t resp_handle_count = 0;

    auto status = zx_channel_read(raw_channel, 0, &req_buf, req_handles, sizeof(req_buf),
                                  fbl::count_of(req_handles), &actual, &req_handle_count);
    if (status != ZX_OK) {
        zxlogf(ERROR, "platform_dev_rxrpc: zx_channel_read failed %d\n", status);
        return status;
    }

    resp_header->txid = req_header->txid;
    uint32_t resp_len = 0;

    switch (req_header->proto_id) {
    case ZX_PROTOCOL_AMLOGIC_CANVAS:
        status = RpcCanvas(req_buf, actual, resp_buf, &resp_len, req_handles, req_handle_count,
                           resp_handles, &resp_handle_count);
        break;
    case ZX_PROTOCOL_CLOCK:
        status = RpcClock(req_buf, actual, resp_buf, &resp_len, req_handles, req_handle_count,
                          resp_handles, &resp_handle_count);
        break;
    case ZX_PROTOCOL_ETH_BOARD:
        status = RpcEthBoard(req_buf, actual, resp_buf, &resp_len, req_handles, req_handle_count,
                             resp_handles, &resp_handle_count);
        break;
    case ZX_PROTOCOL_GPIO:
        status = RpcGpio(req_buf, actual, resp_buf, &resp_len, req_handles, req_handle_count,
                         resp_handles, &resp_handle_count);
        break;
    case ZX_PROTOCOL_I2C:
        status = RpcI2c(req_buf, actual, resp_buf, &resp_len, req_handles, req_handle_count,
                         resp_handles, &resp_handle_count);
        break;
    case ZX_PROTOCOL_PDEV:
        status = RpcPdev(req_buf, actual, resp_buf, &resp_len, req_handles, req_handle_count,
                         resp_handles, &resp_handle_count);
        break;
    case ZX_PROTOCOL_POWER:
        status = RpcPower(req_buf, actual, resp_buf, &resp_len, req_handles, req_handle_count,
                          resp_handles, &resp_handle_count);
        break;
    case ZX_PROTOCOL_SYSMEM:
        status = RpcSysmem(req_buf, actual, resp_buf, &resp_len, req_handles, req_handle_count,
                           resp_handles, &resp_handle_count);
        break;
    case ZX_PROTOCOL_USB_MODE_SWITCH:
        status = RpcUms(req_buf, actual, resp_buf, &resp_len, req_handles, req_handle_count,
                        resp_handles, &resp_handle_count);
        break;
    default:
        zxlogf(ERROR, "%s: unknown protocol %u\n", __func__, req_header->proto_id);
        return ZX_ERR_INTERNAL;
    }

    // set op to match request so zx_channel_write will return our response
    resp_header->status = status;
    status = zx_channel_write(raw_channel, 0, resp_header, resp_len,
                              (resp_handle_count ? resp_handles : nullptr), resp_handle_count);
    if (status != ZX_OK) {
        zxlogf(ERROR, "platform_dev_rxrpc: zx_channel_write failed %d\n", status);
    }
    return status;
}

void Component::DdkUnbind() {
    DdkRemove();
}

void Component::DdkRelease() {
    delete this;
}

const zx_driver_ops_t driver_ops = []() {
    zx_driver_ops_t ops = {};
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = Component::Bind;
    return ops;
}();

} // namespace component

ZIRCON_DRIVER_BEGIN(component, component::driver_ops, "zircon", "0.1", 1)
BI_MATCH() // This driver is excluded from the normal matching process, so this is fine.
ZIRCON_DRIVER_END(component)
