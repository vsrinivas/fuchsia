// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/internal/drivers/fragment/fragment.h"

#include <fuchsia/hardware/goldfish/addressspace/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/fragment-device.h>
#include <lib/ddk/trace/event.h>
#include <stdio.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <atomic>
#include <iterator>
#include <memory>

#include <fbl/algorithm.h>

#include "src/devices/internal/drivers/fragment/fragment-bind.h"
#include "src/devices/internal/drivers/fragment/proxy-protocol.h"

namespace fragment {

namespace {

void MakeUniqueName(char name[ZX_DEVICE_NAME_MAX + 1]) {
  static std::atomic<size_t> unique_id = 0;
  snprintf(name, ZX_DEVICE_NAME_MAX + 1, "fragment-%zu", unique_id.fetch_add(1));
}

}  // namespace

template <typename ProtoClientType, typename ProtoType>
ProtocolClient<ProtoClientType, ProtoType>::ProtocolClient(zx_device_t* parent, uint32_t proto_id) {
  ProtoClientType* protoptr = &proto_client_;
  zx_status_t status = device_open_protocol_session_multibindable(parent, proto_id, &proto_);
  ZX_DEBUG_ASSERT(status == ZX_OK || status == ZX_ERR_NOT_SUPPORTED);
  if (status == ZX_OK) {
    is_session_ = true;
  } else if (status == ZX_ERR_NOT_SUPPORTED) {
    device_get_protocol(parent, proto_id, &proto_);
  }
  *protoptr = ProtoClientType(&proto_);
}

zx_status_t Fragment::Bind(void* ctx, zx_device_t* parent) {
  char name[ZX_DEVICE_NAME_MAX + 1];
  MakeUniqueName(name);
  auto dev = std::make_unique<Fragment>(parent);
  // The thing before the comma will become the process name, if a new process
  // is created
  const char* proxy_args = "composite-device,";
  auto status = dev->DdkAdd(ddk::DeviceAddArgs(name)
                                .set_flags(DEVICE_ADD_NON_BINDABLE | DEVICE_ADD_MUST_ISOLATE)
                                .set_proxy_args(proxy_args));
  if (status == ZX_OK) {
    // devmgr owns the memory now
    __UNUSED auto ptr = dev.release();
  }
  return status;
}

zx_status_t Fragment::RpcAcpi(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                              uint32_t* out_resp_size, zx::handle* req_handles,
                              uint32_t req_handle_count, zx::handle* resp_handles,
                              uint32_t* resp_handle_count) {
  if (!acpi_client_.proto_client().is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  auto* req = reinterpret_cast<const AcpiProxyRequest*>(req_buf);
  if (req_size < sizeof(*req)) {
    zxlogf(ERROR, "%s received %u, expecting %zu", __func__, req_size, sizeof(*req));
    return ZX_ERR_INTERNAL;
  }

  auto* resp = reinterpret_cast<ProxyResponse*>(resp_buf);
  *out_resp_size = sizeof(*resp);

  switch (req->op) {
    case AcpiOp::CONNECT_SERVER:
      if (req_handle_count != 1) {
        zxlogf(ERROR, "%s: expected one handle for %u", __func__, static_cast<uint32_t>(req->op));
        return ZX_ERR_INVALID_ARGS;
      }

      acpi_client_.proto_client().ConnectServer(zx::channel(req_handles[0].release()));
      return ZX_OK;
    default:
      zxlogf(ERROR, "%s: unknown acpi op %u", __func__, static_cast<uint32_t>(req->op));
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t Fragment::RpcCanvas(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                                uint32_t* out_resp_size, zx::handle* req_handles,
                                uint32_t req_handle_count, zx::handle* resp_handles,
                                uint32_t* resp_handle_count) {
  if (!canvas_client_.proto_client().is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  auto* req = reinterpret_cast<const AmlogicCanvasProxyRequest*>(req_buf);
  if (req_size < sizeof(*req)) {
    zxlogf(ERROR, "%s received %u, expecting %zu", __func__, req_size, sizeof(*req));
    return ZX_ERR_INTERNAL;
  }
  auto* resp = reinterpret_cast<AmlogicCanvasProxyResponse*>(resp_buf);
  *out_resp_size = sizeof(*resp);

  switch (req->op) {
    case AmlogicCanvasOp::CONFIG:
      if (req_handle_count != 1) {
        zxlogf(ERROR, "%s received %u handles, expecting 1", __func__, req_handle_count);
        return ZX_ERR_INTERNAL;
      }
      return canvas_client_.proto_client().Config(zx::vmo(std::move(req_handles[0])), req->offset,
                                                  &req->info, &resp->canvas_idx);
    case AmlogicCanvasOp::FREE:
      if (req_handle_count != 0) {
        zxlogf(ERROR, "%s received %u handles, expecting 0", __func__, req_handle_count);
        return ZX_ERR_INTERNAL;
      }
      return canvas_client_.proto_client().Free(req->canvas_idx);
    default:
      zxlogf(ERROR, "%s: unknown clk op %u", __func__, static_cast<uint32_t>(req->op));
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t Fragment::RpcButtons(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                                 uint32_t* out_resp_size, zx::handle* req_handles,
                                 uint32_t req_handle_count, zx::handle* resp_handles,
                                 uint32_t* resp_handle_count) {
  if (!buttons_client_.proto_client().is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  auto* req = reinterpret_cast<const ButtonsProxyRequest*>(req_buf);
  if (req_size < sizeof(*req)) {
    zxlogf(ERROR, "%s received %u, expecting %zu", __func__, req_size, sizeof(*req));
    return ZX_ERR_INTERNAL;
  }
  auto* resp = reinterpret_cast<ButtonsProxyResponse*>(resp_buf);
  *out_resp_size = sizeof(*resp);

  switch (req->op) {
    case ButtonsOp::GET_NOTIFY_CHANNEL:
      if (req_handle_count != 1) {
        zxlogf(ERROR, "%s received %u handles, expecting 1", __func__, req_handle_count);
        return ZX_ERR_INTERNAL;
      }
      return buttons_client_.proto_client().GetChannel(zx::channel(std::move(req_handles[0])));
    default:
      zxlogf(ERROR, "%s: unknown clk op %u", __func__, static_cast<uint32_t>(req->op));
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t Fragment::RpcClock(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                               uint32_t* out_resp_size, zx::handle* req_handles,
                               uint32_t req_handle_count, zx::handle* resp_handles,
                               uint32_t* resp_handle_count) {
  if (!clock_client_.proto_client().is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  auto* req = reinterpret_cast<const ClockProxyRequest*>(req_buf);
  if (req_size < sizeof(*req)) {
    zxlogf(ERROR, "%s received %u, expecting %zu", __func__, req_size, sizeof(*req));
    return ZX_ERR_INTERNAL;
  }
  auto* resp = reinterpret_cast<ClockProxyResponse*>(resp_buf);
  *out_resp_size = sizeof(*resp);

  switch (req->op) {
    case ClockOp::ENABLE:
      return clock_client_.proto_client().Enable();
    case ClockOp::DISABLE:
      return clock_client_.proto_client().Disable();
    case ClockOp::IS_ENABLED:
      return clock_client_.proto_client().IsEnabled(&resp->is_enabled);
    case ClockOp::SET_RATE:
      return clock_client_.proto_client().SetRate(req->rate);
    case ClockOp::QUERY_SUPPORTED_RATE:
      return clock_client_.proto_client().QuerySupportedRate(req->rate, &resp->rate);
    case ClockOp::GET_RATE:
      return clock_client_.proto_client().GetRate(&resp->rate);
    case ClockOp::SET_INPUT:
      return clock_client_.proto_client().SetInput(req->input_idx);
    case ClockOp::GET_NUM_INPUTS:
      return clock_client_.proto_client().GetNumInputs(&resp->num_inputs);
    case ClockOp::GET_INPUT:
      return clock_client_.proto_client().GetInput(&resp->current_input);
    default:
      zxlogf(ERROR, "%s: unknown clk op %u", __func__, static_cast<uint32_t>(req->op));
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t Fragment::RpcEthBoard(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                                  uint32_t* out_resp_size, zx::handle* req_handles,
                                  uint32_t req_handle_count, zx::handle* resp_handles,
                                  uint32_t* resp_handle_count) {
  if (!eth_board_client_.proto_client().is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  auto* req = reinterpret_cast<const EthBoardProxyRequest*>(req_buf);
  if (req_size < sizeof(*req)) {
    zxlogf(ERROR, "%s received %u, expecting %zu", __func__, req_size, sizeof(*req));
    return ZX_ERR_INTERNAL;
  }
  auto* resp = reinterpret_cast<ProxyResponse*>(resp_buf);
  *out_resp_size = sizeof(*resp);

  switch (req->op) {
    case EthBoardOp::RESET_PHY:
      return eth_board_client_.proto_client().ResetPhy();
    default:
      zxlogf(ERROR, "%s: unknown ETH_BOARD op %u", __func__, static_cast<uint32_t>(req->op));
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t Fragment::RpcGoldfishAddressSpace(const uint8_t* req_buf, uint32_t req_size,
                                              uint8_t* resp_buf, uint32_t* out_resp_size,
                                              zx::handle* req_handles, uint32_t req_handle_count,
                                              zx::handle* resp_handles,
                                              uint32_t* resp_handle_count) {
  if (!goldfish_address_space_client_.proto_client().is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto* req = reinterpret_cast<const GoldfishAddressSpaceProxyRequest*>(req_buf);
  if (req_size < sizeof(*req)) {
    zxlogf(ERROR, "%s received %u, expecting %zu", __func__, req_size, sizeof(*req));
    return ZX_ERR_INTERNAL;
  }
  uint32_t expected_handle_count;
  switch (req->op) {
    case GoldfishAddressSpaceOp::OPEN_CHILD_DRIVER:
      expected_handle_count = 1;
      break;
  }
  if (req_handle_count != expected_handle_count) {
    zxlogf(ERROR, "%s received %u handles, expecting %u op %u", __func__, req_handle_count,
           expected_handle_count, static_cast<uint32_t>(req->op));
    return ZX_ERR_INTERNAL;
  }
  auto* resp = reinterpret_cast<GoldfishAddressSpaceProxyResponse*>(resp_buf);
  *out_resp_size = sizeof(*resp);

  switch (req->op) {
    case GoldfishAddressSpaceOp::OPEN_CHILD_DRIVER: {
      zx::channel channel(std::move(req_handles[0]));
      return goldfish_address_space_client_.proto_client().OpenChildDriver(req->type,
                                                                           std::move(channel));
    }
    default:
      zxlogf(ERROR, "%s: unknown GoldfishPipe op %u", __func__, static_cast<uint32_t>(req->op));
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t Fragment::RpcGoldfishPipe(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                                      uint32_t* out_resp_size, zx::handle* req_handles,
                                      uint32_t req_handle_count, zx::handle* resp_handles,
                                      uint32_t* resp_handle_count) {
  if (!goldfish_pipe_client_.proto_client().is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto* req = reinterpret_cast<const GoldfishPipeProxyRequest*>(req_buf);
  if (req_size < sizeof(*req)) {
    zxlogf(ERROR, "%s received %u, expecting %zu", __func__, req_size, sizeof(*req));
    return ZX_ERR_INTERNAL;
  }
  uint32_t expected_handle_count;
  switch (req->op) {
    case GoldfishPipeOp::SET_EVENT:
    case GoldfishPipeOp::CONNECT_SYSMEM:
    case GoldfishPipeOp::REGISTER_SYSMEM_HEAP:
      expected_handle_count = 1;
      break;
    case GoldfishPipeOp::CREATE:
    case GoldfishPipeOp::DESTROY:
    case GoldfishPipeOp::OPEN:
    case GoldfishPipeOp::EXEC:
    case GoldfishPipeOp::GET_BTI:
      expected_handle_count = 0;
      break;
  }
  if (req_handle_count != expected_handle_count) {
    zxlogf(ERROR, "%s received %u handles, expecting %u op %u", __func__, req_handle_count,
           expected_handle_count, static_cast<uint32_t>(req->op));
    return ZX_ERR_INTERNAL;
  }
  auto* resp = reinterpret_cast<GoldfishPipeProxyResponse*>(resp_buf);
  *out_resp_size = sizeof(*resp);

  switch (req->op) {
    case GoldfishPipeOp::CREATE: {
      int32_t id = 0;
      zx::vmo vmo;
      auto status = goldfish_pipe_client_.proto_client().Create(&id, &vmo);
      if (status == ZX_OK) {
        resp->id = id;
        resp_handles[0] = std::move(vmo);
        *resp_handle_count = 1;
      }
      return status;
    }
    case GoldfishPipeOp::DESTROY: {
      goldfish_pipe_client_.proto_client().Destroy(req->id);
      return ZX_OK;
    }
    case GoldfishPipeOp::SET_EVENT: {
      zx::event pipe_event(std::move(req_handles[0]));
      return goldfish_pipe_client_.proto_client().SetEvent(req->id, std::move(pipe_event));
    }
    case GoldfishPipeOp::OPEN: {
      goldfish_pipe_client_.proto_client().Open(req->id);
      return ZX_OK;
    }
    case GoldfishPipeOp::EXEC: {
      goldfish_pipe_client_.proto_client().Exec(req->id);
      return ZX_OK;
    }
    case GoldfishPipeOp::GET_BTI: {
      zx::bti bti;
      auto status = goldfish_pipe_client_.proto_client().GetBti(&bti);
      if (status == ZX_OK) {
        resp_handles[0] = std::move(bti);
        *resp_handle_count = 1;
      }
      return status;
    }
    case GoldfishPipeOp::CONNECT_SYSMEM: {
      zx::channel connection(std::move(req_handles[0]));
      return goldfish_pipe_client_.proto_client().ConnectSysmem(std::move(connection));
    }
    case GoldfishPipeOp::REGISTER_SYSMEM_HEAP: {
      zx::channel connection(std::move(req_handles[0]));
      return goldfish_pipe_client_.proto_client().RegisterSysmemHeap(req->heap,
                                                                     std::move(connection));
    }
    default:
      zxlogf(ERROR, "%s: unknown GoldfishPipe op %u", __func__, static_cast<uint32_t>(req->op));
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t Fragment::RpcGoldfishSync(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                                      uint32_t* out_resp_size, zx::handle* req_handles,
                                      uint32_t req_handle_count, zx::handle* resp_handles,
                                      uint32_t* resp_handle_count) {
  if (!goldfish_sync_client_.proto_client().is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto* req = reinterpret_cast<const GoldfishSyncProxyRequest*>(req_buf);
  if (req_size < sizeof(*req)) {
    zxlogf(ERROR, "%s received %u, expecting %zu", __func__, req_size, sizeof(*req));
    return ZX_ERR_INTERNAL;
  }
  uint32_t expected_handle_count;
  switch (req->op) {
    case GoldfishSyncOp::CREATE_TIMELINE:
      expected_handle_count = 1;
      break;
  }
  if (req_handle_count != expected_handle_count) {
    zxlogf(ERROR, "%s received %u handles, expecting %u op %u", __func__, req_handle_count,
           expected_handle_count, static_cast<uint32_t>(req->op));
    return ZX_ERR_INTERNAL;
  }
  auto* resp = reinterpret_cast<GoldfishSyncProxyResponse*>(resp_buf);
  *out_resp_size = sizeof(*resp);

  switch (req->op) {
    case GoldfishSyncOp::CREATE_TIMELINE: {
      zx::channel request(std::move(req_handles[0]));
      return goldfish_sync_client_.proto_client().CreateTimeline(std::move(request));
    }
    default:
      zxlogf(ERROR, "%s: unknown GoldfishSync op %u", __func__, static_cast<uint32_t>(req->op));
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t Fragment::RpcGpio(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                              uint32_t* out_resp_size, zx::handle* req_handles,
                              uint32_t req_handle_count, zx::handle* resp_handles,
                              uint32_t* resp_handle_count) {
  if (!gpio_client_.proto_client().is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  auto* req = reinterpret_cast<const GpioProxyRequest*>(req_buf);
  if (req_size < sizeof(*req)) {
    zxlogf(ERROR, "%s received %u, expecting %zu", __func__, req_size, sizeof(*req));
    return ZX_ERR_INTERNAL;
  }
  auto* resp = reinterpret_cast<GpioProxyResponse*>(resp_buf);
  *out_resp_size = sizeof(*resp);

  switch (req->op) {
    case GpioOp::CONFIG_IN:
      return gpio_client_.proto_client().ConfigIn(req->flags);
    case GpioOp::CONFIG_OUT:
      return gpio_client_.proto_client().ConfigOut(req->value);
    case GpioOp::SET_ALT_FUNCTION:
      return gpio_client_.proto_client().SetAltFunction(req->alt_function);
    case GpioOp::READ:
      return gpio_client_.proto_client().Read(&resp->value);
    case GpioOp::WRITE:
      return gpio_client_.proto_client().Write(req->value);
    case GpioOp::GET_INTERRUPT: {
      zx::interrupt irq;
      auto status = gpio_client_.proto_client().GetInterrupt(req->flags, &irq);
      if (status == ZX_OK) {
        resp_handles[0] = std::move(irq);
        *resp_handle_count = 1;
      }
      return status;
    }
    case GpioOp::RELEASE_INTERRUPT:
      return gpio_client_.proto_client().ReleaseInterrupt();
    case GpioOp::SET_POLARITY:
      return gpio_client_.proto_client().SetPolarity(req->polarity);
    case GpioOp::SET_DRIVE_STRENGTH:
      return gpio_client_.proto_client().SetDriveStrength(req->ds_ua, &resp->out_actual_ds_ua);
    default:
      zxlogf(ERROR, "%s: unknown GPIO op %u", __func__, static_cast<uint32_t>(req->op));
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t Fragment::RpcHdmi(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                              uint32_t* out_resp_size, zx::handle* req_handles,
                              uint32_t req_handle_count, zx::handle* resp_handles,
                              uint32_t* resp_handle_count) {
  if (!hdmi_client_.proto_client().is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  auto* req = reinterpret_cast<const HdmiProxyRequest*>(req_buf);
  if (req_size < sizeof(*req)) {
    zxlogf(ERROR, "%s received %u, expecting %zu", __func__, req_size, sizeof(*req));
    return ZX_ERR_INTERNAL;
  }
  auto* resp = reinterpret_cast<HdmiProxyResponse*>(resp_buf);
  *out_resp_size = sizeof(*resp);

  switch (req->op) {
    case HdmiOp::CONNECT:
      if (req_handle_count != 1) {
        zxlogf(ERROR, "%s: expected one handle for %u", __func__, static_cast<uint32_t>(req->op));
        return ZX_ERR_INVALID_ARGS;
      }

      hdmi_client_.proto_client().Connect(zx::channel(req_handles[0].release()));
      return ZX_OK;
    default:
      zxlogf(ERROR, "%s: unknown Hdmi op %u", __func__, static_cast<uint32_t>(req->op));
      return ZX_ERR_INTERNAL;
  }
}

void Fragment::I2cTransactCallback(void* cookie, zx_status_t status, const i2c_op_t* op_list,
                                   size_t op_count) {
  auto* ctx = static_cast<I2cTransactContext*>(cookie);
  ctx->result = status;
  if (status == ZX_OK && ctx->read_buf && ctx->read_length) {
    memcpy(ctx->read_buf, op_list[0].data_buffer, ctx->read_length);
  }

  sync_completion_signal(&ctx->completion);
}

zx_status_t Fragment::RpcI2c(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                             uint32_t* out_resp_size, zx::handle* req_handles,
                             uint32_t req_handle_count, zx::handle* resp_handles,
                             uint32_t* resp_handle_count) {
  TRACE_DURATION("i2c", "I2c FragmentProxy RpcI2c");
  if (!i2c_client_.proto_client().is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  auto* req = reinterpret_cast<const I2cProxyRequest*>(req_buf);
  if (req_size < sizeof(*req)) {
    zxlogf(ERROR, "%s received %u, expecting %zu", __func__, req_size, sizeof(*req));
    return ZX_ERR_INTERNAL;
  }
  auto* resp = reinterpret_cast<I2cProxyResponse*>(resp_buf);
  *out_resp_size = sizeof(*resp);
  TRACE_FLOW_END("i2c", "I2c FragmentProxy I2cTransact Flow", req->trace_id);

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

      i2c_client_.proto_client().Transact(i2c_ops, op_count, I2cTransactCallback, &ctx);
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
      return i2c_client_.proto_client().GetMaxTransferSize(&resp->size);
    case I2cOp::GET_INTERRUPT: {
      zx::interrupt irq;
      auto status = i2c_client_.proto_client().GetInterrupt(req->flags, &irq);
      if (status == ZX_OK) {
        resp_handles[0] = std::move(irq);
        *resp_handle_count = 1;
      }
      return status;
    }
    default:
      zxlogf(ERROR, "%s: unknown I2C op %u", __func__, static_cast<uint32_t>(req->op));
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t Fragment::RpcPdev(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                              uint32_t* out_resp_size, zx::handle* req_handles,
                              uint32_t req_handle_count, zx::handle* resp_handles,
                              uint32_t* resp_handle_count) {
  if (!pdev_client_.proto_client().is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  auto* req = reinterpret_cast<const PdevProxyRequest*>(req_buf);
  if (req_size < sizeof(*req)) {
    zxlogf(ERROR, "%s received %u, expecting %zu", __func__, req_size, sizeof(*req));
    return ZX_ERR_INTERNAL;
  }
  auto* resp = reinterpret_cast<PdevProxyResponse*>(resp_buf);
  *out_resp_size = sizeof(*resp);

  switch (req->op) {
    case PdevOp::GET_MMIO: {
      pdev_mmio_t mmio;
      auto status = pdev_client_.proto_client().GetMmio(req->index, &mmio);
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
      auto status = pdev_client_.proto_client().GetInterrupt(req->index, req->flags, &irq);
      if (status == ZX_OK) {
        resp_handles[0] = std::move(irq);
        *resp_handle_count = 1;
      }
      return status;
    }
    case PdevOp::GET_BTI: {
      zx::bti bti;
      auto status = pdev_client_.proto_client().GetBti(req->index, &bti);
      if (status == ZX_OK) {
        resp_handles[0] = std::move(bti);
        *resp_handle_count = 1;
      }
      return status;
    }
    case PdevOp::GET_SMC: {
      zx::resource resource;
      auto status = pdev_client_.proto_client().GetSmc(req->index, &resource);
      if (status == ZX_OK) {
        resp_handles[0] = std::move(resource);
        *resp_handle_count = 1;
      }
      return status;
    }
    case PdevOp::GET_DEVICE_INFO:
      return pdev_client_.proto_client().GetDeviceInfo(&resp->device_info);
    case PdevOp::GET_BOARD_INFO:
      return pdev_client_.proto_client().GetBoardInfo(&resp->board_info);
    default:
      zxlogf(ERROR, "%s: unknown pdev op %u", __func__, static_cast<uint32_t>(req->op));
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t Fragment::RpcPower(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                               uint32_t* out_resp_size, zx::handle* req_handles,
                               uint32_t req_handle_count, zx::handle* resp_handles,
                               uint32_t* resp_handle_count) {
  if (!power_client_.proto_client().is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  auto* req = reinterpret_cast<const PowerProxyRequest*>(req_buf);
  if (req_size < sizeof(*req)) {
    zxlogf(ERROR, "%s received %u, expecting %zu", __FUNCTION__, req_size, sizeof(*req));
    return ZX_ERR_INTERNAL;
  }

  auto* resp = reinterpret_cast<PowerProxyResponse*>(resp_buf);
  *out_resp_size = sizeof(*resp);
  switch (req->op) {
    case PowerOp::REGISTER:
      return power_client_.proto_client().RegisterPowerDomain(req->min_voltage, req->max_voltage);
    case PowerOp::UNREGISTER:
      return power_client_.proto_client().UnregisterPowerDomain();
    case PowerOp::GET_STATUS:
      return power_client_.proto_client().GetPowerDomainStatus(&resp->status);
    case PowerOp::GET_SUPPORTED_VOLTAGE_RANGE:
      return power_client_.proto_client().GetSupportedVoltageRange(&resp->min_voltage,
                                                                   &resp->max_voltage);
    case PowerOp::REQUEST_VOLTAGE:
      return power_client_.proto_client().RequestVoltage(req->set_voltage, &resp->actual_voltage);
    case PowerOp::WRITE_PMIC_CTRL_REG:
      return power_client_.proto_client().WritePmicCtrlReg(req->reg_addr, req->reg_value);
    case PowerOp::READ_PMIC_CTRL_REG:
      return power_client_.proto_client().ReadPmicCtrlReg(req->reg_addr, &resp->reg_value);
    default:
      zxlogf(ERROR, "%s: unknown Power op %u", __func__, static_cast<uint32_t>(req->op));
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t Fragment::RpcPwm(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                             uint32_t* out_resp_size, zx::handle* req_handles,
                             uint32_t req_handle_count, zx::handle* resp_handles,
                             uint32_t* resp_handle_count) {
  if (!pwm_client_.proto_client().is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  auto* req = reinterpret_cast<const PwmProxyRequest*>(req_buf);
  if (req_size < sizeof(*req)) {
    zxlogf(ERROR, "%s received %u, expecting %zu", __FUNCTION__, req_size, sizeof(*req));
    return ZX_ERR_INTERNAL;
  }

  auto* resp = reinterpret_cast<PwmProxyResponse*>(resp_buf);
  *out_resp_size = sizeof(*resp);
  switch (req->op) {
    case PwmOp::GET_CONFIG: {
      if (req->config.mode_config_size > MAX_MODE_CFG_SIZE * sizeof(uint8_t)) {
        return ZX_ERR_NO_SPACE;
      }
      resp->config.mode_config_size = req->config.mode_config_size;
      resp->config.mode_config_buffer = resp->mode_cfg;
      return pwm_client_.proto_client().GetConfig(&resp->config);
    }
    case PwmOp::SET_CONFIG: {
      if (req->config.mode_config_size > MAX_MODE_CFG_SIZE * sizeof(uint8_t)) {
        return ZX_ERR_NO_SPACE;
      }
      uint8_t mode_cfg[MAX_MODE_CFG_SIZE] = {0};
      memcpy(mode_cfg, req->mode_cfg, req->config.mode_config_size);
      pwm_config_t cfg = {req->config.polarity, req->config.period_ns, req->config.duty_cycle,
                          mode_cfg, req->config.mode_config_size};
      return pwm_client_.proto_client().SetConfig(&cfg);
    }
    case PwmOp::ENABLE:
      return pwm_client_.proto_client().Enable();
    case PwmOp::DISABLE:
      return pwm_client_.proto_client().Disable();
    default:
      zxlogf(ERROR, "%s: unknown Pwm op %u", __func__, static_cast<uint32_t>(req->op));
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t Fragment::RpcSpi(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                             uint32_t* out_resp_size, zx::handle* req_handles,
                             uint32_t req_handle_count, zx::handle* resp_handles,
                             uint32_t* resp_handle_count) {
  if (!spi_client_.proto_client().is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  auto req = reinterpret_cast<const SpiProxyRequest*>(req_buf);
  if (req_size < sizeof(*req)) {
    zxlogf(ERROR, "%s received %u, expecting %zu", __func__, req_size, sizeof(*req));
    return ZX_ERR_INTERNAL;
  }

  auto resp = reinterpret_cast<SpiProxyResponse*>(resp_buf);
  *out_resp_size = sizeof(*resp);

  auto txbuf = reinterpret_cast<const uint8_t*>(&req[1]);
  auto rxbuf = reinterpret_cast<uint8_t*>(&resp[1]);

  switch (req->op) {
    case SpiOp::TRANSMIT: {
      return spi_client_.proto_client().Transmit(txbuf, req->length);
    }
    case SpiOp::RECEIVE: {
      size_t actual;
      *out_resp_size += static_cast<uint32_t>(req->length);
      return spi_client_.proto_client().Receive(static_cast<uint32_t>(req->length), rxbuf,
                                                req->length, &actual);
    }
    case SpiOp::EXCHANGE: {
      size_t actual;
      *out_resp_size += static_cast<uint32_t>(req->length);
      return spi_client_.proto_client().Exchange(txbuf, req->length, rxbuf, req->length, &actual);
    }
    case SpiOp::CONNECT_SERVER: {
      if (req_handle_count != 1) {
        zxlogf(ERROR, "%s: expectd 1 VMO, got %u", __func__, req_handle_count);
        return ZX_ERR_INTERNAL;
      }
      spi_client_.proto_client().ConnectServer(zx::channel(std::move(req_handles[0])));
      return ZX_OK;
    }
    default:
      zxlogf(ERROR, "%s: unknown SPI op %u", __func__, static_cast<uint32_t>(req->op));
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t Fragment::RpcSysmem(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                                uint32_t* out_resp_size, zx::handle* req_handles,
                                uint32_t req_handle_count, zx::handle* resp_handles,
                                uint32_t* resp_handle_count) {
  if (!sysmem_client_.proto_client().is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  auto* req = reinterpret_cast<const SysmemProxyRequest*>(req_buf);
  if (req_size < sizeof(*req)) {
    zxlogf(ERROR, "%s received %u, expecting %zu", __func__, req_size, sizeof(*req));
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
    zxlogf(ERROR, "%s received %u handles, expecting %u op %u", __func__, req_handle_count,
           expected_handle_count, static_cast<uint32_t>(req->op));
    return ZX_ERR_INTERNAL;
  }
  *out_resp_size = sizeof(ProxyResponse);

  switch (req->op) {
    case SysmemOp::CONNECT:
      return sysmem_client_.proto_client().Connect(zx::channel(std::move(req_handles[0])));
    case SysmemOp::REGISTER_HEAP:
      return sysmem_client_.proto_client().RegisterHeap(req->heap,
                                                        zx::channel(std::move(req_handles[0])));
    case SysmemOp::REGISTER_SECURE_MEM:
      return sysmem_client_.proto_client().RegisterSecureMem(
          zx::channel(std::move(req_handles[0])));
    case SysmemOp::UNREGISTER_SECURE_MEM:
      return sysmem_client_.proto_client().UnregisterSecureMem();
    default:
      zxlogf(ERROR, "%s: unknown sysmem op %u", __func__, static_cast<uint32_t>(req->op));
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t Fragment::RpcTee(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                             uint32_t* out_resp_size, zx::handle* req_handles,
                             uint32_t req_handle_count, zx::handle* resp_handles,
                             uint32_t* resp_handle_count) {
  if (!tee_client_.proto_client().is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  auto* req = reinterpret_cast<const TeeProxyRequest*>(req_buf);
  if (req_size < sizeof(*req)) {
    zxlogf(ERROR, "%s received %u, expecting %zu", __func__, req_size, sizeof(*req));
    return ZX_ERR_INTERNAL;
  }
  if (req_handle_count < 1 || req_handle_count > 2) {
    zxlogf(ERROR, "%s received %u handles, expecting 1-2", __func__, req_handle_count);
    return ZX_ERR_INTERNAL;
  }
  *out_resp_size = sizeof(ProxyResponse);

  switch (req->op) {
    case TeeOp::CONNECT_TO_APPLICATION: {
      zx::channel tee_device_request(std::move(req_handles[0]));
      zx::channel service_provider;
      if (req_handle_count == 2) {
        service_provider.reset(req_handles[1].release());
      }
      return tee_client_.proto_client().ConnectToApplication(
          &req->application_uuid, std::move(tee_device_request), std::move(service_provider));
    }
    default:
      zxlogf(ERROR, "%s: unknown sysmem op %u", __func__, static_cast<uint32_t>(req->op));
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t Fragment::RpcUms(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                             uint32_t* out_resp_size, zx::handle* req_handles,
                             uint32_t req_handle_count, zx::handle* resp_handles,
                             uint32_t* resp_handle_count) {
  if (!ums_client_.proto_client().is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  auto* req = reinterpret_cast<const UsbModeSwitchProxyRequest*>(req_buf);
  if (req_size < sizeof(*req)) {
    zxlogf(ERROR, "%s received %u, expecting %zu", __FUNCTION__, req_size, sizeof(*req));
    return ZX_ERR_INTERNAL;
  }

  auto* resp = reinterpret_cast<ProxyResponse*>(resp_buf);
  *out_resp_size = sizeof(*resp);
  switch (req->op) {
    case UsbModeSwitchOp::SET_MODE:
      return ums_client_.proto_client().SetMode(req->mode);
    default:
      zxlogf(ERROR, "%s: unknown USB Mode Switch op %u", __func__, static_cast<uint32_t>(req->op));
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t Fragment::RpcCodec(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                               uint32_t* out_resp_size, zx::handle* req_handles,
                               uint32_t req_handle_count, zx::handle* resp_handles,
                               uint32_t* resp_handle_count) {
  if (!codec_client_.proto_client().is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto* req = reinterpret_cast<const CodecProxyRequest*>(req_buf);
  auto* resp = reinterpret_cast<ProxyResponse*>(resp_buf);
  *out_resp_size = sizeof(*resp);

  switch (req->op) {
    case CodecOp::GET_CHANNEL:
      if (req_handle_count != 1) {
        zxlogf(ERROR, "%s received %u handles, expecting 1", __func__, req_handle_count);
        return ZX_ERR_INTERNAL;
      }
      return codec_client_.proto_client().Connect(zx::channel(std::move(req_handles[0])));
    default:
      zxlogf(ERROR, "%s: unknown CODEC op %u", __func__, static_cast<uint32_t>(req->op));
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t Fragment::RpcDai(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                             uint32_t* out_resp_size, zx::handle* req_handles,
                             uint32_t req_handle_count, zx::handle* resp_handles,
                             uint32_t* resp_handle_count) {
  if (!dai_client_.proto_client().is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto* req = reinterpret_cast<const DaiProxyRequest*>(req_buf);
  auto* resp = reinterpret_cast<DaiProxyResponse*>(resp_buf);
  *out_resp_size = sizeof(*resp);

  switch (req->op) {
    case DaiOp::GET_CHANNEL:
      if (req_handle_count != 1) {
        zxlogf(ERROR, "%s received %u handles, expecting 1", __func__, req_handle_count);
        return ZX_ERR_INTERNAL;
      }
      return dai_client_.proto_client().Connect(zx::channel(std::move(req_handles[0])));
    default:
      zxlogf(ERROR, "%s: unknown DAI op %u", __func__, static_cast<uint32_t>(req->op));
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t Fragment::RpcRpmb(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                              uint32_t* out_resp_size, zx::handle* req_handles,
                              uint32_t req_handle_count, zx::handle* resp_handles,
                              uint32_t* resp_handle_count) {
  if (!rpmb_client_.proto_client().is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  auto* req = reinterpret_cast<const RpmbProxyRequest*>(req_buf);
  if (req_size < sizeof(*req)) {
    zxlogf(ERROR, "%s received %u, expecting %zu", __func__, req_size, sizeof(*req));
    return ZX_ERR_INTERNAL;
  }

  auto* resp = reinterpret_cast<ProxyResponse*>(resp_buf);
  *out_resp_size = sizeof(*resp);

  switch (req->op) {
    case RpmbOp::CONNECT_SERVER:
      if (req_handle_count != 1) {
        zxlogf(ERROR, "%s: expected one handle for %u", __func__, static_cast<uint32_t>(req->op));
        return ZX_ERR_INVALID_ARGS;
      }

      rpmb_client_.proto_client().ConnectServer(zx::channel(req_handles[0].release()));
      return ZX_OK;
    default:
      zxlogf(ERROR, "%s: unknown rpmb op %u", __func__, static_cast<uint32_t>(req->op));
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t Fragment::RpcRegisters(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                                   uint32_t* out_resp_size, zx::handle* req_handles,
                                   uint32_t req_handle_count, zx::handle* resp_handles,
                                   uint32_t* resp_handle_count) {
  if (!registers_client_.proto_client().is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  auto* req = reinterpret_cast<const RegistersProxyRequest*>(req_buf);
  if (req_size < sizeof(*req)) {
    zxlogf(ERROR, "%s received %u, expecting %zu", __func__, req_size, sizeof(*req));
    return ZX_ERR_INTERNAL;
  }

  auto* resp = reinterpret_cast<ProxyResponse*>(resp_buf);
  *out_resp_size = sizeof(*resp);

  switch (req->op) {
    case RegistersOp::CONNECT:
      if (req_handle_count != 1) {
        zxlogf(ERROR, "%s: expected one handle for %u", __func__, static_cast<uint32_t>(req->op));
        return ZX_ERR_INVALID_ARGS;
      }

      registers_client_.proto_client().Connect(zx::channel(req_handles[0].release()));
      return ZX_OK;
    default:
      zxlogf(ERROR, "%s: unknown registers op %u", __func__, static_cast<uint32_t>(req->op));
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t Fragment::RpcVreg(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                              uint32_t* out_resp_size, zx::handle* req_handles,
                              uint32_t req_handle_count, zx::handle* resp_handles,
                              uint32_t* resp_handle_count) {
  if (!vreg_client_.proto_client().is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  auto* req = reinterpret_cast<const VregProxyRequest*>(req_buf);
  if (req_size < sizeof(*req)) {
    zxlogf(ERROR, "%s received %u, expecting %zu", __func__, req_size, sizeof(*req));
    return ZX_ERR_INTERNAL;
  }

  auto* resp = reinterpret_cast<VregProxyResponse*>(resp_buf);
  *out_resp_size = sizeof(*resp);

  switch (req->op) {
    case VregOp::SET_VOLTAGE_STEP:
      return vreg_client_.proto_client().SetVoltageStep(req->step);
    case VregOp::GET_VOLTAGE_STEP:
      resp->step = vreg_client_.proto_client().GetVoltageStep();
      return ZX_OK;
    case VregOp::GET_REGULATOR_PARAMS:
      vreg_client_.proto_client().GetRegulatorParams(&resp->params);
      return ZX_OK;
    default:
      zxlogf(ERROR, "%s: unknown vreg op %u", __func__, static_cast<uint32_t>(req->op));
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t Fragment::RpcDsi(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                             uint32_t* out_resp_size, zx::handle* req_handles,
                             uint32_t req_handle_count, zx::handle* resp_handles,
                             uint32_t* resp_handle_count) {
  if (!dsi_client_.proto_client().is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  auto* req = reinterpret_cast<const DsiProxyRequest*>(req_buf);
  if (req_size < sizeof(*req)) {
    zxlogf(ERROR, "%s received %u, expecting %zu", __func__, req_size, sizeof(*req));
    return ZX_ERR_INTERNAL;
  }
  *out_resp_size = sizeof(ProxyResponse);
  switch (req->op) {
    case DsiOp::CONNECT:
      if (req_handle_count != 1) {
        zxlogf(ERROR, "%s: expected one handle for %u", __func__, static_cast<uint32_t>(req->op));
        return ZX_ERR_INVALID_ARGS;
      }
      return dsi_client_.proto_client().Connect(zx::channel(req_handles[0].release()));
    default:
      zxlogf(ERROR, "%s: unknown rpmb op %u", __func__, static_cast<uint32_t>(req->op));
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t Fragment::RpcPowerSensor(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                                     uint32_t* out_resp_size, zx::handle* req_handles,
                                     uint32_t req_handle_count, zx::handle* resp_handles,
                                     uint32_t* resp_handle_count) {
  if (!power_sensor_client_.proto_client().is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  auto* req = reinterpret_cast<const PowerSensorProxyRequest*>(req_buf);
  if (req_size < sizeof(*req)) {
    zxlogf(ERROR, "%s received %u, expecting %zu", __func__, req_size, sizeof(*req));
    return ZX_ERR_INTERNAL;
  }

  auto* resp = reinterpret_cast<ProxyResponse*>(resp_buf);
  *out_resp_size = sizeof(*resp);

  switch (req->op) {
    case PowerSensorOp::CONNECT_SERVER:
      if (req_handle_count != 1) {
        zxlogf(ERROR, "%s: expected one handle for %u", __func__, static_cast<uint32_t>(req->op));
        return ZX_ERR_INVALID_ARGS;
      }

      power_sensor_client_.proto_client().ConnectServer(zx::channel(req_handles[0].release()));
      return ZX_OK;
    default:
      zxlogf(ERROR, "%s: unknown power sensor op %u", __func__, static_cast<uint32_t>(req->op));
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t Fragment::DdkRxrpc(zx_handle_t raw_channel) {
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
                                std::size(req_handles_raw), &actual, &req_handle_count);
  if (status != ZX_OK) {
    zxlogf(ERROR, "platform_dev_rxrpc: zx_channel_read failed %d", status);
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
    case ZX_PROTOCOL_ACPI:
      status = RpcAcpi(req_buf, actual, resp_buf, &resp_len, req_handles, req_handle_count,
                       resp_handles, &resp_handle_count);
      break;
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
    case ZX_PROTOCOL_GOLDFISH_ADDRESS_SPACE:
      status = RpcGoldfishAddressSpace(req_buf, actual, resp_buf, &resp_len, req_handles,
                                       req_handle_count, resp_handles, &resp_handle_count);
      break;
    case ZX_PROTOCOL_GOLDFISH_PIPE:
      status = RpcGoldfishPipe(req_buf, actual, resp_buf, &resp_len, req_handles, req_handle_count,
                               resp_handles, &resp_handle_count);
      break;
    case ZX_PROTOCOL_GOLDFISH_SYNC:
      status = RpcGoldfishSync(req_buf, actual, resp_buf, &resp_len, req_handles, req_handle_count,
                               resp_handles, &resp_handle_count);
      break;
    case ZX_PROTOCOL_GPIO:
      status = RpcGpio(req_buf, actual, resp_buf, &resp_len, req_handles, req_handle_count,
                       resp_handles, &resp_handle_count);
      break;
    case ZX_PROTOCOL_HDMI:
      status = RpcHdmi(req_buf, actual, resp_buf, &resp_len, req_handles, req_handle_count,
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
    case ZX_PROTOCOL_PWM:
      status = RpcPwm(req_buf, actual, resp_buf, &resp_len, req_handles, req_handle_count,
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
    case ZX_PROTOCOL_DAI:
      status = RpcDai(req_buf, actual, resp_buf, &resp_len, req_handles, req_handle_count,
                      resp_handles, &resp_handle_count);
      break;
    case ZX_PROTOCOL_RPMB:
      status = RpcRpmb(req_buf, actual, resp_buf, &resp_len, req_handles, req_handle_count,
                       resp_handles, &resp_handle_count);
      break;
    case ZX_PROTOCOL_REGISTERS:
      status = RpcRegisters(req_buf, actual, resp_buf, &resp_len, req_handles, req_handle_count,
                            resp_handles, &resp_handle_count);
      break;
    case ZX_PROTOCOL_VREG:
      status = RpcVreg(req_buf, actual, resp_buf, &resp_len, req_handles, req_handle_count,
                       resp_handles, &resp_handle_count);
      break;
    case ZX_PROTOCOL_DSI:
      status = RpcDsi(req_buf, actual, resp_buf, &resp_len, req_handles, req_handle_count,
                      resp_handles, &resp_handle_count);
      break;
    case ZX_PROTOCOL_PCI:
      status = RpcPci(req_buf, actual, resp_buf, &resp_len, req_handles, req_handle_count,
                      resp_handles, &resp_handle_count);
      break;
    case ZX_PROTOCOL_POWER_SENSOR:
      status = RpcPowerSensor(req_buf, actual, resp_buf, &resp_len, req_handles, req_handle_count,
                              resp_handles, &resp_handle_count);
      break;

    default:
      zxlogf(ERROR, "%s: unknown protocol %u", __func__, req_header->proto_id);
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
    zxlogf(ERROR, "platform_dev_rxrpc: zx_channel_write failed %d", status);
  }
  return status;
}

zx_status_t Fragment::DdkGetProtocol(uint32_t proto_id, void* out_protocol) {
  switch (proto_id) {
    case ZX_PROTOCOL_ACPI: {
      if (!acpi_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      acpi_client_.proto_client().GetProto(static_cast<acpi_protocol_t*>(out_protocol));
      return ZX_OK;
    }
    case ZX_PROTOCOL_AMLOGIC_CANVAS: {
      if (!canvas_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      canvas_client_.proto_client().GetProto(static_cast<amlogic_canvas_protocol_t*>(out_protocol));
      return ZX_OK;
    }
    case ZX_PROTOCOL_BUTTONS: {
      if (!buttons_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      buttons_client_.proto_client().GetProto(static_cast<buttons_protocol_t*>(out_protocol));
      return ZX_OK;
    }
    case ZX_PROTOCOL_CLOCK: {
      if (!clock_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      clock_client_.proto_client().GetProto(static_cast<clock_protocol_t*>(out_protocol));
      return ZX_OK;
    }
    case ZX_PROTOCOL_ETH_BOARD: {
      if (!eth_board_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      eth_board_client_.proto_client().GetProto(static_cast<eth_board_protocol_t*>(out_protocol));
      return ZX_OK;
    }
    case ZX_PROTOCOL_GOLDFISH_ADDRESS_SPACE: {
      if (!goldfish_address_space_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      goldfish_address_space_client_.proto_client().GetProto(
          static_cast<goldfish_address_space_protocol_t*>(out_protocol));
      return ZX_OK;
    }
    case ZX_PROTOCOL_GOLDFISH_PIPE: {
      if (!goldfish_pipe_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      goldfish_pipe_client_.proto_client().GetProto(
          static_cast<goldfish_pipe_protocol_t*>(out_protocol));
      return ZX_OK;
    }
    case ZX_PROTOCOL_GOLDFISH_SYNC: {
      if (!goldfish_sync_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      goldfish_sync_client_.proto_client().GetProto(
          static_cast<goldfish_sync_protocol_t*>(out_protocol));
      return ZX_OK;
    }
    case ZX_PROTOCOL_GPIO: {
      if (!gpio_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      gpio_client_.proto_client().GetProto(static_cast<gpio_protocol_t*>(out_protocol));
      return ZX_OK;
    }
    case ZX_PROTOCOL_HDMI: {
      if (!hdmi_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      hdmi_client_.proto_client().GetProto(static_cast<hdmi_protocol_t*>(out_protocol));
      return ZX_OK;
    }
    case ZX_PROTOCOL_I2C: {
      if (!i2c_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      i2c_client_.proto_client().GetProto(static_cast<i2c_protocol_t*>(out_protocol));
      return ZX_OK;
    }
    case ZX_PROTOCOL_CODEC: {
      if (!codec_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      codec_client_.proto_client().GetProto(static_cast<codec_protocol_t*>(out_protocol));
      return ZX_OK;
    }
    case ZX_PROTOCOL_DAI: {
      if (!dai_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      dai_client_.proto_client().GetProto(static_cast<dai_protocol_t*>(out_protocol));
      return ZX_OK;
    }
    case ZX_PROTOCOL_PDEV: {
      if (!pdev_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      pdev_client_.proto_client().GetProto(static_cast<pdev_protocol_t*>(out_protocol));
      return ZX_OK;
    }
    case ZX_PROTOCOL_PWM: {
      if (!pwm_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      pwm_client_.proto_client().GetProto(static_cast<pwm_protocol_t*>(out_protocol));
      return ZX_OK;
    }
    case ZX_PROTOCOL_SPI: {
      if (!spi_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      spi_client_.proto_client().GetProto(static_cast<spi_protocol_t*>(out_protocol));
      return ZX_OK;
    }
    case ZX_PROTOCOL_SYSMEM: {
      if (!sysmem_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      sysmem_client_.proto_client().GetProto(static_cast<sysmem_protocol_t*>(out_protocol));
      return ZX_OK;
    }
    case ZX_PROTOCOL_TEE: {
      if (!tee_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      tee_client_.proto_client().GetProto(static_cast<tee_protocol_t*>(out_protocol));
      return ZX_OK;
    }
    case ZX_PROTOCOL_USB_MODE_SWITCH: {
      if (!ums_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      ums_client_.proto_client().GetProto(static_cast<usb_mode_switch_protocol_t*>(out_protocol));
      return ZX_OK;
    }
    case ZX_PROTOCOL_POWER: {
      if (!power_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      power_client_.proto_client().GetProto(static_cast<power_protocol_t*>(out_protocol));
      return ZX_OK;
    }
    case ZX_PROTOCOL_POWER_IMPL: {
      if (!power_impl_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      power_impl_client_.proto_client().GetProto(static_cast<power_impl_protocol_t*>(out_protocol));
      return ZX_OK;
    }
    case ZX_PROTOCOL_DSI_IMPL: {
      if (!dsi_impl_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      dsi_impl_client_.proto_client().GetProto(static_cast<dsi_impl_protocol_t*>(out_protocol));
      return ZX_OK;
    }

    case ZX_PROTOCOL_SDIO: {
      if (!sdio_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      sdio_client_.proto_client().GetProto(static_cast<sdio_protocol_t*>(out_protocol));
      return ZX_OK;
    }

    case ZX_PROTOCOL_THERMAL: {
      if (!thermal_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      thermal_client_.proto_client().GetProto(static_cast<thermal_protocol_t*>(out_protocol));
      return ZX_OK;
    }
    case ZX_PROTOCOL_ISP: {
      if (!isp_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      isp_client_.proto_client().GetProto(static_cast<isp_protocol_t*>(out_protocol));
      return ZX_OK;
    }
    case ZX_PROTOCOL_SHARED_DMA: {
      if (!shared_dma_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      shared_dma_client_.proto_client().GetProto(static_cast<shared_dma_protocol_t*>(out_protocol));
      return ZX_OK;
    }

    case ZX_PROTOCOL_USB_PHY: {
      if (!usb_phy_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      usb_phy_client_.proto_client().GetProto(static_cast<usb_phy_protocol_t*>(out_protocol));
      return ZX_OK;
    }

    case ZX_PROTOCOL_MIPI_CSI: {
      if (!mipi_csi_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      mipi_csi_client_.proto_client().GetProto(static_cast<mipi_csi_protocol_t*>(out_protocol));
      return ZX_OK;
    }

    case ZX_PROTOCOL_CAMERA_SENSOR2: {
      if (!camera_sensor2_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      camera_sensor2_client_.proto_client().GetProto(
          static_cast<camera_sensor2_protocol_t*>(out_protocol));
      return ZX_OK;
    }
    case ZX_PROTOCOL_SCPI: {
      if (!scpi_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      scpi_client_.proto_client().GetProto(static_cast<scpi_protocol_t*>(out_protocol));
      return ZX_OK;
    }
    case ZX_PROTOCOL_GDC: {
      if (!gdc_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      gdc_client_.proto_client().GetProto(static_cast<gdc_protocol_t*>(out_protocol));
      return ZX_OK;
    }

    case ZX_PROTOCOL_GE2D: {
      if (!ge2d_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      ge2d_client_.proto_client().GetProto(static_cast<ge2d_protocol_t*>(out_protocol));
      return ZX_OK;
    }

    case ZX_PROTOCOL_RPMB: {
      if (!rpmb_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      rpmb_client_.proto_client().GetProto(static_cast<rpmb_protocol_t*>(out_protocol));
      return ZX_OK;
    }

    case ZX_PROTOCOL_REGISTERS: {
      if (!registers_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      registers_client_.proto_client().GetProto(static_cast<registers_protocol_t*>(out_protocol));
      return ZX_OK;
    }

    case ZX_PROTOCOL_VREG: {
      if (!vreg_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      vreg_client_.proto_client().GetProto(static_cast<vreg_protocol_t*>(out_protocol));
      return ZX_OK;
    }

    case ZX_PROTOCOL_PCI: {
      if (!pci_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      pci_client_.proto_client().GetProto(static_cast<pci_protocol_t*>(out_protocol));
      return ZX_OK;
    }

    case ZX_PROTOCOL_POWER_SENSOR: {
      if (!power_sensor_client_.proto_client().is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      power_sensor_client_.proto_client().GetProto(
          static_cast<power_sensor_protocol_t*>(out_protocol));
      return ZX_OK;
    }

    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

void Fragment::DdkRelease() { delete this; }

const zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Fragment::Bind;
  return ops;
}();

}  // namespace fragment

ZIRCON_DRIVER(fragment, fragment::driver_ops, "zircon", "0.1");
