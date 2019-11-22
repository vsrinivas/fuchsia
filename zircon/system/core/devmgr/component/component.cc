// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "component.h"

#include <memory>

#include <ddk/debug.h>
#include <fbl/algorithm.h>

#include "proxy-protocol.h"

namespace component {

zx_status_t Component::Bind(void* ctx, zx_device_t* parent) {
  auto dev = std::make_unique<Component>(parent);
  // The thing before the comma will become the process name, if a new process
  // is created
  const char* proxy_args = "composite-device,";
  auto status = dev->DdkAdd("component", DEVICE_ADD_NON_BINDABLE | DEVICE_ADD_MUST_ISOLATE,
                            nullptr /* props */, 0 /* prop count */, 0 /* proto id */, proxy_args);
  if (status == ZX_OK) {
    // devmgr owns the memory now
    __UNUSED auto ptr = dev.release();
  }
  return status;
}

zx_status_t Component::RpcCanvas(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                                 uint32_t* out_resp_size, zx::handle* req_handles,
                                 uint32_t req_handle_count, zx::handle* resp_handles,
                                 uint32_t* resp_handle_count) {
  if (!canvas_.is_valid()) {
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
      return canvas_.Config(zx::vmo(std::move(req_handles[0])), req->offset, &req->info,
                            &resp->canvas_idx);
    case AmlogicCanvasOp::FREE:
      if (req_handle_count != 0) {
        zxlogf(ERROR, "%s received %u handles, expecting 0\n", __func__, req_handle_count);
        return ZX_ERR_INTERNAL;
      }
      return canvas_.Free(req->canvas_idx);
    default:
      zxlogf(ERROR, "%s: unknown clk op %u\n", __func__, static_cast<uint32_t>(req->op));
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t Component::RpcButtons(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                                  uint32_t* out_resp_size, zx::handle* req_handles,
                                  uint32_t req_handle_count, zx::handle* resp_handles,
                                  uint32_t* resp_handle_count) {
  if (!buttons_.is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  auto* req = reinterpret_cast<const ButtonsProxyRequest*>(req_buf);
  if (req_size < sizeof(*req)) {
    zxlogf(ERROR, "%s received %u, expecting %zu\n", __func__, req_size, sizeof(*req));
    return ZX_ERR_INTERNAL;
  }
  auto* resp = reinterpret_cast<ButtonsProxyResponse*>(resp_buf);
  *out_resp_size = sizeof(*resp);

  switch (req->op) {
    case ButtonsOp::GET_NOTIFY_CHANNEL:
      if (req_handle_count != 1) {
        zxlogf(ERROR, "%s received %u handles, expecting 1\n", __func__, req_handle_count);
        return ZX_ERR_INTERNAL;
      }
      return buttons_.GetChannel(zx::channel(std::move(req_handles[0])));
    default:
      zxlogf(ERROR, "%s: unknown clk op %u\n", __func__, static_cast<uint32_t>(req->op));
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t Component::RpcClock(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                                uint32_t* out_resp_size, zx::handle* req_handles,
                                uint32_t req_handle_count, zx::handle* resp_handles,
                                uint32_t* resp_handle_count) {
  if (!clock_.is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  auto* req = reinterpret_cast<const ClockProxyRequest*>(req_buf);
  if (req_size < sizeof(*req)) {
    zxlogf(ERROR, "%s received %u, expecting %zu\n", __func__, req_size, sizeof(*req));
    return ZX_ERR_INTERNAL;
  }
  auto* resp = reinterpret_cast<ClockProxyResponse*>(resp_buf);
  *out_resp_size = sizeof(*resp);

  switch (req->op) {
    case ClockOp::ENABLE:
      return clock_.Enable();
    case ClockOp::DISABLE:
      return clock_.Disable();
    case ClockOp::IS_ENABLED:
      return clock_.IsEnabled(&resp->is_enabled);
    case ClockOp::SET_RATE:
      return clock_.SetRate(req->rate);
    case ClockOp::QUERY_SUPPORTED_RATE:
      return clock_.QuerySupportedRate(req->rate, &resp->rate);
    case ClockOp::GET_RATE:
      return clock_.GetRate(&resp->rate);
    case ClockOp::SET_INPUT:
      return clock_.SetInput(req->input_idx);
    case ClockOp::GET_NUM_INPUTS:
      return clock_.GetNumInputs(&resp->num_inputs);
    case ClockOp::GET_INPUT:
      return clock_.GetInput(&resp->current_input);
    default:
      zxlogf(ERROR, "%s: unknown clk op %u\n", __func__, static_cast<uint32_t>(req->op));
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t Component::RpcEthBoard(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                                   uint32_t* out_resp_size, zx::handle* req_handles,
                                   uint32_t req_handle_count, zx::handle* resp_handles,
                                   uint32_t* resp_handle_count) {
  if (!eth_board_.is_valid()) {
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
      return eth_board_.ResetPhy();
    default:
      zxlogf(ERROR, "%s: unknown ETH_BOARD op %u\n", __func__, static_cast<uint32_t>(req->op));
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t Component::RpcGpio(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                               uint32_t* out_resp_size, zx::handle* req_handles,
                               uint32_t req_handle_count, zx::handle* resp_handles,
                               uint32_t* resp_handle_count) {
  if (!gpio_.is_valid()) {
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
      return gpio_.ConfigIn(req->flags);
    case GpioOp::CONFIG_OUT:
      return gpio_.ConfigOut(req->value);
    case GpioOp::SET_ALT_FUNCTION:
      return gpio_.SetAltFunction(req->alt_function);
    case GpioOp::READ:
      return gpio_.Read(&resp->value);
    case GpioOp::WRITE:
      return gpio_.Write(req->value);
    case GpioOp::GET_INTERRUPT: {
      zx::interrupt irq;
      auto status = gpio_.GetInterrupt(req->flags, &irq);
      if (status == ZX_OK) {
        resp_handles[0] = std::move(irq);
        *resp_handle_count = 1;
      }
      return status;
    }
    case GpioOp::RELEASE_INTERRUPT:
      return gpio_.ReleaseInterrupt();
    case GpioOp::SET_POLARITY:
      return gpio_.SetPolarity(req->polarity);
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

void Component::CodecTransactCallback(void* cookie, zx_status_t status,
                                      const dai_supported_formats_t* formats_list,
                                      size_t formats_count) {
  auto* out = reinterpret_cast<CodecTransactContext*>(cookie);
  auto* p = reinterpret_cast<uint8_t*>(out->buffer);
  memcpy(p, &formats_count, sizeof(size_t));
  p += sizeof(size_t);
  if (static_cast<size_t>(p - reinterpret_cast<uint8_t*>(out->buffer) +
                          sizeof(dai_supported_formats_t) * formats_count) > out->size) {
    out->status = ZX_ERR_INTERNAL;
    sync_completion_signal(&out->completion);
    return;
  }
  memcpy(p, formats_list, sizeof(dai_supported_formats_t) * formats_count);
  p += sizeof(dai_supported_formats_t) * formats_count;
  for (size_t i = 0; i < formats_count; ++i) {
    // clang-format off
        size_t size_number_of_channels = formats_list[i].number_of_channels_count * sizeof(uint32_t);
        size_t size_sample_formats     = formats_list[i].sample_formats_count     * sizeof(sample_format_t);
        size_t size_justify_formats    = formats_list[i].justify_formats_count    * sizeof(justify_format_t);
        size_t size_frame_rates        = formats_list[i].frame_rates_count        * sizeof(uint32_t);
        size_t size_bits_per_channel   = formats_list[i].bits_per_channel_count   * sizeof(uint8_t);
        size_t size_bits_per_sample    = formats_list[i].bits_per_sample_count    * sizeof(uint8_t);
        if (p - reinterpret_cast<uint8_t*>(out->buffer) + size_number_of_channels +
            size_sample_formats + size_justify_formats + size_frame_rates + size_bits_per_channel +
            size_bits_per_sample > out->size) {
            out->status = ZX_ERR_INTERNAL;
            return;
        }
        memcpy(p, formats_list[i].number_of_channels_list, size_number_of_channels); p += size_number_of_channels;
        memcpy(p, formats_list[i].sample_formats_list,     size_sample_formats);     p += size_sample_formats;
        memcpy(p, formats_list[i].justify_formats_list,    size_justify_formats);    p += size_justify_formats;
        memcpy(p, formats_list[i].frame_rates_list,        size_frame_rates);        p += size_frame_rates;
        memcpy(p, formats_list[i].bits_per_channel_list,   size_bits_per_channel);   p += size_bits_per_channel;
        memcpy(p, formats_list[i].bits_per_sample_list,    size_bits_per_sample);    p += size_bits_per_sample;
    // clang-format on
  }
  out->size = p - reinterpret_cast<uint8_t*>(out->buffer);  // Override out->size with actual size.
  out->status = status;
  sync_completion_signal(&out->completion);
}

zx_status_t Component::RpcI2c(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                              uint32_t* out_resp_size, zx::handle* req_handles,
                              uint32_t req_handle_count, zx::handle* resp_handles,
                              uint32_t* resp_handle_count) {
  if (!i2c_.is_valid()) {
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

      i2c_.Transact(i2c_ops, op_count, I2cTransactCallback, &ctx);
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
      return i2c_.GetMaxTransferSize(&resp->size);
    case I2cOp::GET_INTERRUPT: {
      zx::interrupt irq;
      auto status = i2c_.GetInterrupt(req->flags, &irq);
      if (status == ZX_OK) {
        resp_handles[0] = std::move(irq);
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
                               uint32_t* out_resp_size, zx::handle* req_handles,
                               uint32_t req_handle_count, zx::handle* resp_handles,
                               uint32_t* resp_handle_count) {
  if (!pdev_.is_valid()) {
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
      auto status = pdev_.GetMmio(req->index, &mmio);
      if (status == ZX_OK) {
        resp->offset = mmio.offset;
        resp->size = mmio.size;
        resp_handles[0].reset(mmio.vmo);
        *resp_handle_count = 1;
      }
      return status;
    }
    case PdevOp::GET_INTERRUPT: {
      zx::interrupt irq;
      auto status = pdev_.GetInterrupt(req->index, req->flags, &irq);
      if (status == ZX_OK) {
        resp_handles[0] = std::move(irq);
        *resp_handle_count = 1;
      }
      return status;
    }
    case PdevOp::GET_BTI: {
      zx::bti bti;
      auto status = pdev_.GetBti(req->index, &bti);
      if (status == ZX_OK) {
        resp_handles[0] = std::move(bti);
        *resp_handle_count = 1;
      }
      return status;
    }
    case PdevOp::GET_SMC: {
      zx::resource resource;
      auto status = pdev_.GetSmc(req->index, &resource);
      if (status == ZX_OK) {
        resp_handles[0] = std::move(resource);
        *resp_handle_count = 1;
      }
      return status;
    }
    case PdevOp::GET_DEVICE_INFO:
      return pdev_.GetDeviceInfo(&resp->device_info);
    case PdevOp::GET_BOARD_INFO:
      return pdev_.GetBoardInfo(&resp->board_info);
    default:
      zxlogf(ERROR, "%s: unknown pdev op %u\n", __func__, static_cast<uint32_t>(req->op));
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t Component::RpcPower(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                                uint32_t* out_resp_size, zx::handle* req_handles,
                                uint32_t req_handle_count, zx::handle* resp_handles,
                                uint32_t* resp_handle_count) {
  if (!power_.is_valid()) {
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
      return power_.EnablePowerDomain();
    case PowerOp::DISABLE:
      return power_.DisablePowerDomain();
    case PowerOp::GET_STATUS:
      return power_.GetPowerDomainStatus(&resp->status);
    case PowerOp::GET_SUPPORTED_VOLTAGE_RANGE:
      return power_.GetSupportedVoltageRange(&resp->min_voltage, &resp->max_voltage);
    case PowerOp::REQUEST_VOLTAGE:
      return power_.RequestVoltage(req->set_voltage, &resp->actual_voltage);
    case PowerOp::WRITE_PMIC_CTRL_REG:
      return power_.WritePmicCtrlReg(req->reg_addr, req->reg_value);
    case PowerOp::READ_PMIC_CTRL_REG:
      return power_.ReadPmicCtrlReg(req->reg_addr, &resp->reg_value);
    default:
      zxlogf(ERROR, "%s: unknown Power op %u\n", __func__, static_cast<uint32_t>(req->op));
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t Component::RpcSpi(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                              uint32_t* out_resp_size, zx::handle* req_handles,
                              uint32_t req_handle_count, zx::handle* resp_handles,
                              uint32_t* resp_handle_count) {
  if (!spi_.is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  auto req = reinterpret_cast<const SpiProxyRequest*>(req_buf);
  if (req_size < sizeof(*req)) {
    zxlogf(ERROR, "%s received %u, expecting %zu\n", __func__, req_size, sizeof(*req));
    return ZX_ERR_INTERNAL;
  }

  auto resp = reinterpret_cast<SpiProxyResponse*>(resp_buf);
  *out_resp_size = sizeof(*resp);

  auto txbuf = reinterpret_cast<const uint8_t*>(&req[1]);
  auto rxbuf = reinterpret_cast<uint8_t*>(&resp[1]);

  switch (req->op) {
    case SpiOp::TRANSMIT: {
      return spi_.Transmit(txbuf, req->length);
    }
    case SpiOp::RECEIVE: {
      size_t actual;
      *out_resp_size += static_cast<uint32_t>(req->length);
      return spi_.Receive(static_cast<uint32_t>(req->length), rxbuf, req->length, &actual);
    }
    case SpiOp::EXCHANGE: {
      size_t actual;
      *out_resp_size += static_cast<uint32_t>(req->length);
      return spi_.Exchange(txbuf, req->length, rxbuf, req->length, &actual);
    }
    default:
      zxlogf(ERROR, "%s: unknown SPI op %u\n", __func__, static_cast<uint32_t>(req->op));
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t Component::RpcSysmem(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                                 uint32_t* out_resp_size, zx::handle* req_handles,
                                 uint32_t req_handle_count, zx::handle* resp_handles,
                                 uint32_t* resp_handle_count) {
  if (!sysmem_.is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  auto* req = reinterpret_cast<const SysmemProxyRequest*>(req_buf);
  if (req_size < sizeof(*req)) {
    zxlogf(ERROR, "%s received %u, expecting %zu\n", __func__, req_size, sizeof(*req));
    return ZX_ERR_INTERNAL;
  }
  uint32_t expected_handle_count;
  switch (req->op) {
    case SysmemOp::CONNECT:
    case SysmemOp::REGISTER_HEAP:
    case SysmemOp::REGISTER_SECURE_MEM:
      expected_handle_count = 1;
      break;
    case SysmemOp::UNREGISTER_SECURE_MEM:
      expected_handle_count = 0;
      break;
  }
  if (req_handle_count != expected_handle_count) {
    zxlogf(ERROR, "%s received %u handles, expecting %u op %u\n", __func__, req_handle_count,
           expected_handle_count, static_cast<uint32_t>(req->op));
    return ZX_ERR_INTERNAL;
  }
  *out_resp_size = sizeof(ProxyResponse);

  switch (req->op) {
    case SysmemOp::CONNECT:
      return sysmem_.Connect(zx::channel(std::move(req_handles[0])));
    case SysmemOp::REGISTER_HEAP:
      return sysmem_.RegisterHeap(req->heap, zx::channel(std::move(req_handles[0])));
    case SysmemOp::REGISTER_SECURE_MEM:
      return sysmem_.RegisterSecureMem(zx::channel(std::move(req_handles[0])));
    case SysmemOp::UNREGISTER_SECURE_MEM:
      return sysmem_.UnregisterSecureMem();
    default:
      zxlogf(ERROR, "%s: unknown sysmem op %u\n", __func__, static_cast<uint32_t>(req->op));
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t Component::RpcTee(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                              uint32_t* out_resp_size, zx::handle* req_handles,
                              uint32_t req_handle_count, zx::handle* resp_handles,
                              uint32_t* resp_handle_count) {
  if (!tee_.is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  auto* req = reinterpret_cast<const TeeProxyRequest*>(req_buf);
  if (req_size < sizeof(*req)) {
    zxlogf(ERROR, "%s received %u, expecting %zu\n", __func__, req_size, sizeof(*req));
    return ZX_ERR_INTERNAL;
  }
  if (req_handle_count < 1 || req_handle_count > 2) {
    zxlogf(ERROR, "%s received %u handles, expecting 1-2\n", __func__, req_handle_count);
    return ZX_ERR_INTERNAL;
  }
  *out_resp_size = sizeof(ProxyResponse);

  switch (req->op) {
    case TeeOp::CONNECT: {
      zx::channel tee_device_request(std::move(req_handles[0]));
      zx::channel service_provider;
      if (req_handle_count == 2) {
        service_provider.reset(req_handles[1].release());
      }
      return tee_.Connect(std::move(tee_device_request), std::move(service_provider));
    }
    default:
      zxlogf(ERROR, "%s: unknown sysmem op %u\n", __func__, static_cast<uint32_t>(req->op));
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t Component::RpcUms(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                              uint32_t* out_resp_size, zx::handle* req_handles,
                              uint32_t req_handle_count, zx::handle* resp_handles,
                              uint32_t* resp_handle_count) {
  if (!ums_.is_valid()) {
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
      return ums_.SetMode(req->mode);
    default:
      zxlogf(ERROR, "%s: unknown USB Mode Switch op %u\n", __func__,
             static_cast<uint32_t>(req->op));
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t Component::RpcCodec(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                                uint32_t* out_resp_size, zx::handle* req_handles,
                                uint32_t req_handle_count, zx::handle* resp_handles,
                                uint32_t* resp_handle_count) {
  static constexpr uint32_t kTimeoutSecs = 1;
  if (!codec_.is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto* req = reinterpret_cast<const CodecProxyRequest*>(req_buf);
  auto* resp = reinterpret_cast<ProxyResponse*>(resp_buf);
  *out_resp_size = sizeof(*resp);

  switch (req->op) {
    case CodecOp::RESET: {
      struct AsyncOut {
        sync_completion_t completion;
        zx_status_t status;
      } out;
      codec_.Reset(
          [](void* cookie, zx_status_t status) {
            auto* out = reinterpret_cast<AsyncOut*>(cookie);
            out->status = status;
            sync_completion_signal(&out->completion);
          },
          &out);
      auto status = sync_completion_wait(&out.completion, zx::sec(kTimeoutSecs).get());
      if (status == ZX_OK) {
        status = out.status;
      }
      return status;
    }
    case CodecOp::GET_INFO: {
      auto* resp = reinterpret_cast<CodecInfoProxyResponse*>(resp_buf);
      *out_resp_size = sizeof(*resp);
      struct AsyncOut {
        sync_completion_t completion;
        CodecInfoProxyResponse* resp;
      } out;
      out.resp = resp;
      codec_.GetInfo(
          [](void* cookie, const info_t* info) {
            auto* out = reinterpret_cast<AsyncOut*>(cookie);
            strncpy(out->resp->unique_id, info->unique_id, kMaxCodecStringSize - 1);
            strncpy(out->resp->manufacturer, info->manufacturer, kMaxCodecStringSize - 1);
            strncpy(out->resp->product_name, info->product_name, kMaxCodecStringSize - 1);
            sync_completion_signal(&out->completion);
          },
          &out);
      return sync_completion_wait(&out.completion, zx::sec(kTimeoutSecs).get());
    }
    case CodecOp::IS_BRIDGEABLE: {
      auto* resp = reinterpret_cast<CodecIsBridgeableProxyResponse*>(resp_buf);
      *out_resp_size = sizeof(*resp);
      struct AsyncOut {
        sync_completion_t completion;
        bool supports_bridged_mode;
      } out;
      codec_.IsBridgeable(
          [](void* cookie, bool supports_bridged_mode) {
            auto* out = reinterpret_cast<AsyncOut*>(cookie);
            out->supports_bridged_mode = supports_bridged_mode;
            sync_completion_signal(&out->completion);
          },
          &out);
      auto status = sync_completion_wait(&out.completion, zx::sec(kTimeoutSecs).get());
      resp->supports_bridged_mode = out.supports_bridged_mode;
      return status;
    }
    case CodecOp::SET_BRIDGED_MODE: {
      auto* req = reinterpret_cast<const CodecSetBridgedProxyRequest*>(req_buf);
      codec_.SetBridgedMode(req->enable_bridged_mode, [](void* cookie) {}, nullptr);
      return ZX_OK;
    }
    case CodecOp::GET_DAI_FORMATS: {
      CodecTransactContext out = {};
      out.buffer = &resp[1];
      // Set out.size to max available size.
      out.size = kProxyMaxTransferSize - sizeof(*resp);

      codec_.GetDaiFormats(CodecTransactCallback, &out);
      auto status = sync_completion_wait(&out.completion, zx::sec(kTimeoutSecs).get());
      if (status == ZX_OK) {
        status = out.status;
      }
      if (status == ZX_OK) {
        *out_resp_size = static_cast<uint32_t>(sizeof(*resp) + out.size);
      }
      return status;
    }
    case CodecOp::SET_DAI_FORMAT: {
      struct AsyncOut {
        sync_completion_t completion;
        zx_status_t status;
      } out;
      auto* req = reinterpret_cast<const CodecDaiFormatProxyRequest*>(req_buf);
      dai_format_t format = req->format;  // Copy format and edit any pointers next.
      format.channels_to_use_list = req->channels_to_use;

      codec_.SetDaiFormat(&format,
                          [](void* cookie, zx_status_t status) {
                            auto* out = reinterpret_cast<AsyncOut*>(cookie);
                            out->status = status;
                            sync_completion_signal(&out->completion);
                          },
                          &out);
      auto status = sync_completion_wait(&out.completion, zx::sec(kTimeoutSecs).get());
      if (status == ZX_OK) {
        status = out.status;
      }
      return status;
    }
    case CodecOp::GET_GAIN_FORMAT: {
      auto* resp = reinterpret_cast<CodecGainFormatProxyResponse*>(resp_buf);
      *out_resp_size = sizeof(*resp);
      struct AsyncOut {
        sync_completion_t completion;
        gain_format_t format;
      } out;
      codec_.GetGainFormat(
          [](void* cookie, const gain_format_t* format) {
            auto* out = reinterpret_cast<AsyncOut*>(cookie);
            out->format = *format;
            sync_completion_signal(&out->completion);
          },
          &out);
      auto status = sync_completion_wait(&out.completion, zx::sec(kTimeoutSecs).get());
      if (status == ZX_OK) {
        resp->format = out.format;
      }
      return status;
    }
    case CodecOp::GET_GAIN_STATE: {
      auto* resp = reinterpret_cast<CodecGainStateProxyResponse*>(resp_buf);
      *out_resp_size = sizeof(*resp);
      struct AsyncOut {
        sync_completion_t completion;
        gain_state_t state;
      } out;
      codec_.GetGainState(
          [](void* cookie, const gain_state_t* state) {
            auto* out = reinterpret_cast<AsyncOut*>(cookie);
            out->state = *state;
            sync_completion_signal(&out->completion);
          },
          &out);
      auto status = sync_completion_wait(&out.completion, zx::sec(kTimeoutSecs).get());
      if (status == ZX_OK) {
        resp->state = out.state;
      }
      return status;
    }
    case CodecOp::SET_GAIN_STATE: {
      auto* req = reinterpret_cast<const CodecGainStateProxyRequest*>(req_buf);
      codec_.SetGainState(&req->state, [](void* cookie) {}, nullptr);
      return ZX_OK;
    }
    case CodecOp::GET_PLUG_STATE: {
      auto* resp = reinterpret_cast<CodecPlugStateProxyResponse*>(resp_buf);
      *out_resp_size = sizeof(*resp);
      struct AsyncOut {
        sync_completion_t completion;
        plug_state_t state;
      } out;
      codec_.GetPlugState(
          [](void* cookie, const plug_state_t* state) {
            auto* out = reinterpret_cast<AsyncOut*>(cookie);
            out->state = *state;
            sync_completion_signal(&out->completion);
          },
          &out);
      auto status = sync_completion_wait(&out.completion, zx::sec(kTimeoutSecs).get());
      if (status == ZX_OK) {
        resp->plug_state = out.state;
      }
      return status;
    }
    default:
      zxlogf(ERROR, "%s: unknown CODEC op %u\n", __func__, static_cast<uint32_t>(req->op));
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

  // Ensure all response messages are fully initialized.
  uint8_t resp_buf[kProxyMaxTransferSize] = {};
  auto* req_header = reinterpret_cast<ProxyRequest*>(&req_buf);
  auto* resp_header = reinterpret_cast<ProxyResponse*>(&resp_buf);
  uint32_t actual;

  zx_handle_t req_handles_raw[ZX_CHANNEL_MAX_MSG_HANDLES];
  uint32_t req_handle_count;

  auto status = zx_channel_read(raw_channel, 0, &req_buf, req_handles_raw, sizeof(req_buf),
                                fbl::count_of(req_handles_raw), &actual, &req_handle_count);
  if (status != ZX_OK) {
    zxlogf(ERROR, "platform_dev_rxrpc: zx_channel_read failed %d\n", status);
    return status;
  }

  // There is some expense in constructing/destructing these.  If that becomes an issue, we could
  // create an incremental construction array type to prevent constructing the extras.
  zx::handle req_handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  for (uint32_t handle_index = 0; handle_index < req_handle_count; ++handle_index) {
    // req_handles_raw handle values are ignored after this point, so no need to clear them.
    req_handles[handle_index].reset(req_handles_raw[handle_index]);
  }

  constexpr uint32_t kMaxRespHandles = 1;
  zx::handle resp_handles[kMaxRespHandles];
  uint32_t resp_handle_count = 0;

  resp_header->txid = req_header->txid;
  uint32_t resp_len = 0;

  switch (req_header->proto_id) {
    case ZX_PROTOCOL_AMLOGIC_CANVAS:
      status = RpcCanvas(req_buf, actual, resp_buf, &resp_len, req_handles, req_handle_count,
                         resp_handles, &resp_handle_count);
      break;
    case ZX_PROTOCOL_BUTTONS:
      status = RpcButtons(req_buf, actual, resp_buf, &resp_len, req_handles, req_handle_count,
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
    case ZX_PROTOCOL_SPI:
      status = RpcSpi(req_buf, actual, resp_buf, &resp_len, req_handles, req_handle_count,
                      resp_handles, &resp_handle_count);
      break;
    case ZX_PROTOCOL_SYSMEM:
      status = RpcSysmem(req_buf, actual, resp_buf, &resp_len, req_handles, req_handle_count,
                         resp_handles, &resp_handle_count);
      break;
    case ZX_PROTOCOL_TEE:
      status = RpcTee(req_buf, actual, resp_buf, &resp_len, req_handles, req_handle_count,
                      resp_handles, &resp_handle_count);
      break;
    case ZX_PROTOCOL_USB_MODE_SWITCH:
      status = RpcUms(req_buf, actual, resp_buf, &resp_len, req_handles, req_handle_count,
                      resp_handles, &resp_handle_count);
      break;
    case ZX_PROTOCOL_CODEC:
      status = RpcCodec(req_buf, actual, resp_buf, &resp_len, req_handles, req_handle_count,
                        resp_handles, &resp_handle_count);
      break;
    default:
      zxlogf(ERROR, "%s: unknown protocol %u\n", __func__, req_header->proto_id);
      return ZX_ERR_INTERNAL;
  }

  ZX_DEBUG_ASSERT(resp_handle_count <= kMaxRespHandles);

  zx_handle_t resp_handles_raw[kMaxRespHandles];
  for (uint32_t handle_index = 0; handle_index < resp_handle_count; ++handle_index) {
    // Will be transferred or closed by zx_channel_write().
    resp_handles_raw[handle_index] = resp_handles[handle_index].release();
  }

  // set op to match request so zx_channel_write will return our response
  resp_header->status = status;
  status = zx_channel_write(raw_channel, 0, resp_header, resp_len,
                            (resp_handle_count ? resp_handles_raw : nullptr), resp_handle_count);
  if (status != ZX_OK) {
    zxlogf(ERROR, "platform_dev_rxrpc: zx_channel_write failed %d\n", status);
  }
  return status;
}

void Component::DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }

void Component::DdkRelease() { delete this; }

const zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Component::Bind;
  return ops;
}();

}  // namespace component

ZIRCON_DRIVER_BEGIN(component, component::driver_ops, "zircon", "0.1", 1)
BI_MATCH()  // This driver is excluded from the normal matching process, so this is fine.
ZIRCON_DRIVER_END(component)
