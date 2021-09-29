// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/internal/drivers/fragment/fragment-proxy.h"

#include <lib/ddk/debug.h>
#include <lib/ddk/trace/event.h>
#include <lib/sync/completion.h>

#include <memory>

#include "src/devices/internal/drivers/fragment/fragment-proxy-bind.h"

namespace fragment {

zx_status_t FragmentProxy::Create(void* ctx, zx_device_t* parent, const char* name,
                                  const char* args, zx_handle_t raw_rpc) {
  zx::channel rpc(raw_rpc);
  auto dev = std::make_unique<FragmentProxy>(parent, std::move(rpc));
  auto status = dev->DdkAdd("fragment-proxy", DEVICE_ADD_NON_BINDABLE);
  if (status == ZX_OK) {
    // devmgr owns the memory now
    __UNUSED auto ptr = dev.release();
  }
  return status;
}

zx_status_t FragmentProxy::DdkGetProtocol(uint32_t proto_id, void* out) {
  auto* proto = static_cast<ddk::AnyProtocol*>(out);
  proto->ctx = this;

  switch (proto_id) {
    case ZX_PROTOCOL_ACPI:
      proto->ops = &acpi_protocol_ops_;
      return ZX_OK;
    case ZX_PROTOCOL_AMLOGIC_CANVAS:
      proto->ops = &amlogic_canvas_protocol_ops_;
      return ZX_OK;
    case ZX_PROTOCOL_BUTTONS:
      proto->ops = &buttons_protocol_ops_;
      return ZX_OK;
    case ZX_PROTOCOL_CODEC:
      proto->ops = &codec_protocol_ops_;
      return ZX_OK;
    case ZX_PROTOCOL_DAI:
      proto->ops = &dai_protocol_ops_;
      return ZX_OK;
    case ZX_PROTOCOL_CLOCK:
      proto->ops = &clock_protocol_ops_;
      return ZX_OK;
    case ZX_PROTOCOL_ETH_BOARD:
      proto->ops = &eth_board_protocol_ops_;
      return ZX_OK;
    case ZX_PROTOCOL_GOLDFISH_ADDRESS_SPACE:
      proto->ops = &goldfish_address_space_protocol_ops_;
      return ZX_OK;
    case ZX_PROTOCOL_GOLDFISH_PIPE:
      proto->ops = &goldfish_pipe_protocol_ops_;
      return ZX_OK;
    case ZX_PROTOCOL_GOLDFISH_SYNC:
      proto->ops = &goldfish_sync_protocol_ops_;
      return ZX_OK;
    case ZX_PROTOCOL_GPIO:
      proto->ops = &gpio_protocol_ops_;
      return ZX_OK;
    case ZX_PROTOCOL_HDMI:
      proto->ops = &hdmi_protocol_ops_;
      return ZX_OK;
    case ZX_PROTOCOL_I2C:
      proto->ops = &i2c_protocol_ops_;
      return ZX_OK;
    case ZX_PROTOCOL_PDEV:
      proto->ops = &pdev_protocol_ops_;
      return ZX_OK;
    case ZX_PROTOCOL_POWER:
      proto->ops = &power_protocol_ops_;
      return ZX_OK;
    case ZX_PROTOCOL_PWM:
      proto->ops = &pwm_protocol_ops_;
      return ZX_OK;
    case ZX_PROTOCOL_REGISTERS:
      proto->ops = &registers_protocol_ops_;
      return ZX_OK;
    case ZX_PROTOCOL_RPMB:
      proto->ops = &rpmb_protocol_ops_;
      return ZX_OK;
    case ZX_PROTOCOL_SPI:
      proto->ops = &spi_protocol_ops_;
      return ZX_OK;
    case ZX_PROTOCOL_SYSMEM:
      proto->ops = &sysmem_protocol_ops_;
      return ZX_OK;
    case ZX_PROTOCOL_TEE:
      proto->ops = &tee_protocol_ops_;
      return ZX_OK;
    case ZX_PROTOCOL_VREG:
      proto->ops = &vreg_protocol_ops_;
      return ZX_OK;
    case ZX_PROTOCOL_USB_MODE_SWITCH:
      proto->ops = &usb_mode_switch_protocol_ops_;
      return ZX_OK;
    case ZX_PROTOCOL_DSI:
      proto->ops = &dsi_protocol_ops_;
      return ZX_OK;
    case ZX_PROTOCOL_PCI:
      proto->ops = &pci_protocol_ops_;
      return ZX_OK;
    case ZX_PROTOCOL_POWER_SENSOR:
      proto->ops = &power_sensor_protocol_ops_;
      return ZX_OK;
    default:
      zxlogf(ERROR, "%s unsupported protocol \'%u\'", __func__, proto_id);
      return ZX_ERR_NOT_SUPPORTED;
  }
}

void FragmentProxy::DdkRelease() { delete this; }

zx_status_t FragmentProxy::Rpc(const ProxyRequest* req, size_t req_length, ProxyResponse* resp,
                               size_t resp_length, const zx_handle_t* in_handles,
                               size_t in_handle_count, zx_handle_t* out_handles,
                               size_t out_handle_count, size_t* out_actual) {
  uint32_t resp_size, handle_count;

  zx_channel_call_args_t args = {
      .wr_bytes = req,
      .wr_handles = in_handles,
      .rd_bytes = resp,
      .rd_handles = out_handles,
      .wr_num_bytes = static_cast<uint32_t>(req_length),
      .wr_num_handles = static_cast<uint32_t>(in_handle_count),
      .rd_num_bytes = static_cast<uint32_t>(resp_length),
      .rd_num_handles = static_cast<uint32_t>(out_handle_count),
  };
  auto status = rpc_.call(0, zx::time::infinite(), &args, &resp_size, &handle_count);
  if (status != ZX_OK) {
    return status;
  }

  status = resp->status;

  if (status == ZX_OK && resp_size < sizeof(*resp)) {
    zxlogf(ERROR, "PlatformProxy::Rpc resp_size too short: %u", resp_size);
    status = ZX_ERR_INTERNAL;
    goto fail;
  } else if (status == ZX_OK && handle_count != out_handle_count) {
    zxlogf(ERROR, "PlatformProxy::Rpc handle count %u expected %zu", handle_count,
           out_handle_count);
    status = ZX_ERR_INTERNAL;
    goto fail;
  }

  if (out_actual) {
    *out_actual = resp_size;
  }

fail:
  if (status != ZX_OK) {
    for (uint32_t i = 0; i < handle_count; i++) {
      zx_handle_close(out_handles[i]);
    }
  }
  return status;
}

zx_status_t FragmentProxy::AmlogicCanvasConfig(zx::vmo vmo, size_t offset,
                                               const canvas_info_t* info, uint8_t* out_canvas_idx) {
  AmlogicCanvasProxyRequest req = {};
  AmlogicCanvasProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_AMLOGIC_CANVAS;
  req.op = AmlogicCanvasOp::CONFIG;
  req.offset = offset;
  req.info = *info;
  zx_handle_t handle = vmo.release();

  auto status =
      Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), &handle, 1, nullptr, 0, nullptr);
  if (status != ZX_OK) {
    return status;
  }
  *out_canvas_idx = resp.canvas_idx;
  return ZX_OK;
}

zx_status_t FragmentProxy::AmlogicCanvasFree(uint8_t canvas_idx) {
  AmlogicCanvasProxyRequest req = {};
  AmlogicCanvasProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_AMLOGIC_CANVAS;
  req.op = AmlogicCanvasOp::FREE;
  req.canvas_idx = canvas_idx;

  return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t FragmentProxy::ButtonsGetChannel(zx::channel chan) {
  ButtonsProxyRequest req = {};
  ButtonsProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_BUTTONS;
  req.op = ButtonsOp::GET_NOTIFY_CHANNEL;
  zx_handle_t handle = chan.release();

  auto status =
      Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), &handle, 1, nullptr, 0, nullptr);
  if (status != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

zx_status_t FragmentProxy::CodecConnect(zx::channel chan) {
  CodecProxyRequest req = {};
  CodecProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_CODEC;
  req.op = CodecOp::GET_CHANNEL;
  zx_handle_t handle = chan.release();

  auto status =
      Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), &handle, 1, nullptr, 0, nullptr);
  if (status != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

zx_status_t FragmentProxy::DaiConnect(zx::channel chan) {
  DaiProxyRequest req = {};
  DaiProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_DAI;
  req.op = DaiOp::GET_CHANNEL;
  zx_handle_t handle = chan.release();

  auto status =
      Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), &handle, 1, nullptr, 0, nullptr);
  if (status != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

zx_status_t FragmentProxy::ClockEnable() {
  ClockProxyRequest req = {};
  ClockProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_CLOCK;
  req.op = ClockOp::ENABLE;

  return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t FragmentProxy::ClockDisable() {
  ClockProxyRequest req = {};
  ClockProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_CLOCK;
  req.op = ClockOp::DISABLE;

  return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t FragmentProxy::ClockIsEnabled(bool* out_enabled) {
  ClockProxyRequest req = {};
  ClockProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_CLOCK;
  req.op = ClockOp::IS_ENABLED;

  auto status = Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
  if (status == ZX_OK) {
    *out_enabled = resp.is_enabled;
  }
  return status;
}

zx_status_t FragmentProxy::ClockSetRate(uint64_t hz) {
  ClockProxyRequest req = {};
  ClockProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_CLOCK;
  req.op = ClockOp::SET_RATE;
  req.rate = hz;

  return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t FragmentProxy::ClockQuerySupportedRate(uint64_t max_rate,
                                                   uint64_t* out_max_supported_rate) {
  ClockProxyRequest req = {};
  ClockProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_CLOCK;
  req.op = ClockOp::QUERY_SUPPORTED_RATE;
  req.rate = max_rate;

  auto status = Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
  if (status == ZX_OK) {
    *out_max_supported_rate = resp.rate;
  }
  return status;
}

zx_status_t FragmentProxy::ClockGetRate(uint64_t* out_current_rate) {
  ClockProxyRequest req = {};
  ClockProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_CLOCK;
  req.op = ClockOp::GET_RATE;

  auto status = Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
  if (status == ZX_OK) {
    *out_current_rate = resp.rate;
  }
  return status;
}

zx_status_t FragmentProxy::ClockSetInput(uint32_t idx) {
  ClockProxyRequest req = {};
  ClockProxyResponse resp = {};

  req.header.proto_id = ZX_PROTOCOL_CLOCK;
  req.op = ClockOp::SET_INPUT;
  req.input_idx = idx;

  return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t FragmentProxy::ClockGetNumInputs(uint32_t* out_num_inputs) {
  ClockProxyRequest req = {};
  ClockProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_CLOCK;
  req.op = ClockOp::GET_NUM_INPUTS;

  auto status = Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));

  if (status == ZX_OK) {
    *out_num_inputs = resp.num_inputs;
  }

  return status;
}

zx_status_t FragmentProxy::ClockGetInput(uint32_t* out_current_input) {
  ClockProxyRequest req = {};
  ClockProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_CLOCK;
  req.op = ClockOp::GET_INPUT;

  auto status = Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));

  if (status == ZX_OK) {
    *out_current_input = resp.current_input;
  }

  return status;
}

zx_status_t FragmentProxy::EthBoardResetPhy() {
  EthBoardProxyRequest req = {};
  ProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_ETH_BOARD;
  req.op = EthBoardOp::RESET_PHY;

  return Rpc(&req.header, sizeof(req), &resp, sizeof(resp));
}

zx_status_t FragmentProxy::GoldfishAddressSpaceOpenChildDriver(
    address_space_child_driver_type_t type, zx::channel request) {
  GoldfishAddressSpaceProxyRequest req = {};
  GoldfishAddressSpaceProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_GOLDFISH_ADDRESS_SPACE;
  req.op = GoldfishAddressSpaceOp::OPEN_CHILD_DRIVER;

  zx_handle_t channel = request.release();
  return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), &channel, 1, nullptr, 0,
             nullptr);
}

zx_status_t FragmentProxy::GoldfishPipeCreate(int32_t* out_id, zx::vmo* out_vmo) {
  GoldfishPipeProxyRequest req = {};
  GoldfishPipeProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_GOLDFISH_PIPE;
  req.op = GoldfishPipeOp::CREATE;

  zx_status_t status = Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), nullptr, 0,
                           out_vmo->reset_and_get_address(), 1, nullptr);
  if (status == ZX_OK) {
    *out_id = resp.id;
  }
  return status;
}

void FragmentProxy::GoldfishPipeDestroy(int32_t id) {
  GoldfishPipeProxyRequest req = {};
  GoldfishPipeProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_GOLDFISH_PIPE;
  req.op = GoldfishPipeOp::DESTROY;
  req.id = id;

  Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), nullptr, 0, nullptr, 0, nullptr);
}

zx_status_t FragmentProxy::GoldfishPipeSetEvent(int32_t id, zx::event pipe_event) {
  GoldfishPipeProxyRequest req = {};
  GoldfishPipeProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_GOLDFISH_PIPE;
  req.op = GoldfishPipeOp::SET_EVENT;
  req.id = id;

  zx_handle_t event = pipe_event.release();
  return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), &event, 1, nullptr, 0, nullptr);
}

void FragmentProxy::GoldfishPipeOpen(int32_t id) {
  GoldfishPipeProxyRequest req = {};
  GoldfishPipeProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_GOLDFISH_PIPE;
  req.op = GoldfishPipeOp::OPEN;
  req.id = id;

  Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), nullptr, 0, nullptr, 0, nullptr);
}

void FragmentProxy::GoldfishPipeExec(int32_t id) {
  GoldfishPipeProxyRequest req = {};
  GoldfishPipeProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_GOLDFISH_PIPE;
  req.op = GoldfishPipeOp::EXEC;
  req.id = id;

  Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), nullptr, 0, nullptr, 0, nullptr);
}

zx_status_t FragmentProxy::GoldfishPipeGetBti(zx::bti* out_bti) {
  GoldfishPipeProxyRequest req = {};
  GoldfishPipeProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_GOLDFISH_PIPE;
  req.op = GoldfishPipeOp::GET_BTI;

  return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), nullptr, 0,
             out_bti->reset_and_get_address(), 1, nullptr);
}

zx_status_t FragmentProxy::GoldfishPipeConnectSysmem(zx::channel connection) {
  GoldfishPipeProxyRequest req = {};
  GoldfishPipeProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_GOLDFISH_PIPE;
  req.op = GoldfishPipeOp::CONNECT_SYSMEM;

  zx_handle_t channel = connection.release();
  return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), &channel, 1, nullptr, 0,
             nullptr);
}

zx_status_t FragmentProxy::GoldfishPipeRegisterSysmemHeap(uint64_t heap, zx::channel connection) {
  GoldfishPipeProxyRequest req = {};
  GoldfishPipeProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_GOLDFISH_PIPE;
  req.op = GoldfishPipeOp::REGISTER_SYSMEM_HEAP;
  req.heap = heap;

  zx_handle_t channel = connection.release();
  return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), &channel, 1, nullptr, 0,
             nullptr);
}

zx_status_t FragmentProxy::GoldfishSyncCreateTimeline(zx::channel request) {
  GoldfishSyncProxyRequest req = {};
  GoldfishSyncProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_GOLDFISH_SYNC;
  req.op = GoldfishSyncOp::CREATE_TIMELINE;

  zx_handle_t channel = request.release();
  return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), &channel, 1, nullptr, 0,
             nullptr);
}

zx_status_t FragmentProxy::GpioConfigIn(uint32_t flags) {
  GpioProxyRequest req = {};
  GpioProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_GPIO;
  req.op = GpioOp::CONFIG_IN;
  req.flags = flags;

  return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t FragmentProxy::GpioConfigOut(uint8_t initial_value) {
  GpioProxyRequest req = {};
  GpioProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_GPIO;
  req.op = GpioOp::CONFIG_OUT;
  req.value = initial_value;

  return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t FragmentProxy::GpioSetAltFunction(uint64_t function) {
  GpioProxyRequest req = {};
  GpioProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_GPIO;
  req.op = GpioOp::SET_ALT_FUNCTION;
  req.alt_function = function;

  return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t FragmentProxy::GpioGetInterrupt(uint32_t flags, zx::interrupt* out_irq) {
  GpioProxyRequest req = {};
  GpioProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_GPIO;
  req.op = GpioOp::GET_INTERRUPT;
  req.flags = flags;

  return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), nullptr, 0,
             out_irq->reset_and_get_address(), 1, nullptr);
}

zx_status_t FragmentProxy::GpioSetPolarity(uint32_t polarity) {
  GpioProxyRequest req = {};
  GpioProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_GPIO;
  req.op = GpioOp::SET_POLARITY;
  req.polarity = polarity;

  return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t FragmentProxy::GpioSetDriveStrength(uint64_t ds_ua, uint64_t* out_actual_ds_ua) {
  GpioProxyRequest req = {};
  GpioProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_GPIO;
  req.op = GpioOp::SET_DRIVE_STRENGTH;
  req.ds_ua = ds_ua;

  auto status = Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
  if ((status == ZX_OK) && out_actual_ds_ua) {
    *out_actual_ds_ua = resp.out_actual_ds_ua;
  }
  return status;
}

zx_status_t FragmentProxy::GpioReleaseInterrupt() {
  GpioProxyRequest req = {};
  GpioProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_GPIO;
  req.op = GpioOp::RELEASE_INTERRUPT;

  return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t FragmentProxy::GpioRead(uint8_t* out_value) {
  GpioProxyRequest req = {};
  GpioProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_GPIO;
  req.op = GpioOp::READ;

  auto status = Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));

  if (status != ZX_OK) {
    return status;
  }
  *out_value = resp.value;
  return ZX_OK;
}

zx_status_t FragmentProxy::GpioWrite(uint8_t value) {
  GpioProxyRequest req = {};
  GpioProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_GPIO;
  req.op = GpioOp::WRITE;
  req.value = value;

  return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
}

void FragmentProxy::HdmiConnect(zx::channel chan) {
  HdmiProxyRequest req = {};
  HdmiProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_HDMI;
  req.op = HdmiOp::CONNECT;

  zx_handle_t handle = chan.release();
  Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), &handle, 1, nullptr, 0, nullptr);
}

void FragmentProxy::I2cTransact(const i2c_op_t* op_list, size_t op_count,
                                i2c_transact_callback callback, void* cookie) {
  size_t writes_length = 0;
  size_t reads_length = 0;
  for (size_t i = 0; i < op_count; ++i) {
    if (op_list[i].is_read) {
      reads_length += op_list[i].data_size;
    } else {
      writes_length += op_list[i].data_size;
    }
  }
  if (!writes_length && !reads_length) {
    callback(cookie, ZX_ERR_INVALID_ARGS, nullptr, 0);
    return;
  }

  size_t req_length = sizeof(I2cProxyRequest) + op_count * sizeof(I2cProxyOp) + writes_length;
  if (req_length >= kProxyMaxTransferSize) {
    return callback(cookie, ZX_ERR_BUFFER_TOO_SMALL, nullptr, 0);
  }

  TRACE_DURATION("i2c", "I2c FragmentProxy I2cTransact");
  uint8_t req_buffer[kProxyMaxTransferSize];
  auto req = reinterpret_cast<I2cProxyRequest*>(req_buffer);
  req->header.proto_id = ZX_PROTOCOL_I2C;
  req->op = I2cOp::TRANSACT;
  req->op_count = op_count;
  if (TRACE_ENABLED()) {
    req->trace_id = TRACE_NONCE();
    TRACE_FLOW_BEGIN("i2c", "I2c FragmentProxy I2cTransact Flow", req->trace_id);
  }

  auto rpc_ops = reinterpret_cast<I2cProxyOp*>(&req[1]);
  ZX_ASSERT(op_count < I2C_MAX_RW_OPS);
  for (size_t i = 0; i < op_count; ++i) {
    rpc_ops[i].length = op_list[i].data_size;
    rpc_ops[i].is_read = op_list[i].is_read;
    rpc_ops[i].stop = op_list[i].stop;
  }
  uint8_t* p_writes = reinterpret_cast<uint8_t*>(rpc_ops) + op_count * sizeof(I2cProxyOp);
  for (size_t i = 0; i < op_count; ++i) {
    if (!op_list[i].is_read) {
      memcpy(p_writes, op_list[i].data_buffer, op_list[i].data_size);
      p_writes += op_list[i].data_size;
    }
  }

  const size_t resp_length = sizeof(I2cProxyResponse) + reads_length;
  if (resp_length >= kProxyMaxTransferSize) {
    callback(cookie, ZX_ERR_INVALID_ARGS, nullptr, 0);
    return;
  }
  uint8_t resp_buffer[kProxyMaxTransferSize];
  auto* rsp = reinterpret_cast<I2cProxyResponse*>(resp_buffer);
  size_t actual;
  auto status = Rpc(&req->header, static_cast<uint32_t>(req_length), &rsp->header,
                    static_cast<uint32_t>(resp_length), nullptr, 0, nullptr, 0, &actual);
  if (status != ZX_OK) {
    callback(cookie, status, nullptr, 0);
    return;
  }

  // TODO(voydanoff) This proxying code actually implements i2c_transact synchronously
  // due to the fact that it is unsafe to respond asynchronously on the devmgr rxrpc channel.
  // In the future we may want to redo the plumbing to allow this to be truly asynchronous.

  if (actual != resp_length) {
    status = ZX_ERR_INTERNAL;
  } else {
    status = rsp->header.status;
  }
  i2c_op_t read_ops[I2C_MAX_RW_OPS];
  size_t read_ops_cnt = 0;
  uint8_t* p_reads = reinterpret_cast<uint8_t*>(rsp + 1);
  for (size_t i = 0; i < op_count; ++i) {
    if (op_list[i].is_read) {
      read_ops[read_ops_cnt] = op_list[i];
      read_ops[read_ops_cnt].data_buffer = p_reads;
      read_ops_cnt++;
      p_reads += op_list[i].data_size;
    }
  }
  callback(cookie, status, read_ops, read_ops_cnt);
}

zx_status_t FragmentProxy::I2cGetMaxTransferSize(size_t* out_size) {
  I2cProxyRequest req = {};
  I2cProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_I2C;
  req.op = I2cOp::GET_MAX_TRANSFER_SIZE;

  auto status = Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
  if (status != ZX_OK) {
    return status;
  }
  *out_size = resp.size;
  return ZX_OK;
}

zx_status_t FragmentProxy::PDevGetMmio(uint32_t index, pdev_mmio_t* out_mmio) {
  PdevProxyRequest req = {};
  PdevProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_PDEV;
  req.op = PdevOp::GET_MMIO;
  req.index = index;

  auto status = Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), nullptr, 0,
                    &out_mmio->vmo, 1, nullptr);
  if (status == ZX_OK) {
    out_mmio->offset = resp.offset;
    out_mmio->size = resp.size;
  }
  return status;
}

zx_status_t FragmentProxy::PDevGetInterrupt(uint32_t index, uint32_t flags,
                                            zx::interrupt* out_irq) {
  PdevProxyRequest req = {};
  PdevProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_PDEV;
  req.op = PdevOp::GET_INTERRUPT;
  req.index = index;
  req.flags = flags;

  return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), nullptr, 0,
             out_irq->reset_and_get_address(), 1, nullptr);
}

zx_status_t FragmentProxy::PDevGetBti(uint32_t index, zx::bti* out_bti) {
  PdevProxyRequest req = {};
  PdevProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_PDEV;
  req.op = PdevOp::GET_BTI;
  req.index = index;

  return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), nullptr, 0,
             out_bti->reset_and_get_address(), 1, nullptr);
}

zx_status_t FragmentProxy::PDevGetSmc(uint32_t index, zx::resource* out_resource) {
  PdevProxyRequest req = {};
  PdevProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_PDEV;
  req.op = PdevOp::GET_SMC;
  req.index = index;

  return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), nullptr, 0,
             out_resource->reset_and_get_address(), 1, nullptr);
}

zx_status_t FragmentProxy::PDevGetDeviceInfo(pdev_device_info_t* out_info) {
  PdevProxyRequest req = {};
  PdevProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_PDEV;
  req.op = PdevOp::GET_DEVICE_INFO;

  auto status = Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
  if (status != ZX_OK) {
    return status;
  }
  memcpy(out_info, &resp.device_info, sizeof(*out_info));
  return ZX_OK;
}

zx_status_t FragmentProxy::PDevGetBoardInfo(pdev_board_info_t* out_info) {
  PdevProxyRequest req = {};
  PdevProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_PDEV;
  req.op = PdevOp::GET_BOARD_INFO;

  auto status = Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
  if (status != ZX_OK) {
    return status;
  }
  memcpy(out_info, &resp.board_info, sizeof(*out_info));
  return ZX_OK;
}

zx_status_t FragmentProxy::PDevDeviceAdd(uint32_t index, const device_add_args_t* args,
                                         zx_device_t** device) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t FragmentProxy::PDevGetProtocol(uint32_t proto_id, uint32_t index, void* out_protocol,
                                           size_t protocol_size, size_t* protocol_actual) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t FragmentProxy::PowerRegisterPowerDomain(uint32_t min_voltage, uint32_t max_voltage) {
  PowerProxyRequest req = {};
  PowerProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_POWER;
  req.op = PowerOp::REGISTER;
  req.min_voltage = min_voltage;
  req.max_voltage = max_voltage;

  return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t FragmentProxy::PowerUnregisterPowerDomain() {
  PowerProxyRequest req = {};
  PowerProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_POWER;
  req.op = PowerOp::UNREGISTER;

  return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t FragmentProxy::PowerGetPowerDomainStatus(power_domain_status_t* out_status) {
  PowerProxyRequest req = {};
  PowerProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_POWER;
  req.op = PowerOp::GET_STATUS;

  auto status = Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
  if (status != ZX_OK) {
    return status;
  }
  *out_status = resp.status;
  return status;
}

zx_status_t FragmentProxy::PowerGetSupportedVoltageRange(uint32_t* min_voltage,
                                                         uint32_t* max_voltage) {
  PowerProxyRequest req = {};
  PowerProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_POWER;
  req.op = PowerOp::GET_SUPPORTED_VOLTAGE_RANGE;

  auto status = Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
  if (status != ZX_OK) {
    return status;
  }
  *min_voltage = resp.min_voltage;
  *max_voltage = resp.max_voltage;
  return status;
}

zx_status_t FragmentProxy::PowerRequestVoltage(uint32_t voltage, uint32_t* actual_voltage) {
  PowerProxyRequest req = {};
  PowerProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_POWER;
  req.op = PowerOp::REQUEST_VOLTAGE;
  req.set_voltage = voltage;

  auto status = Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
  if (status != ZX_OK) {
    return status;
  }
  *actual_voltage = resp.actual_voltage;
  return status;
}

zx_status_t FragmentProxy::PowerGetCurrentVoltage(uint32_t index, uint32_t* current_voltage) {
  PowerProxyRequest req = {};
  PowerProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_POWER;
  req.op = PowerOp::GET_CURRENT_VOLTAGE;

  auto status = Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
  if (status != ZX_OK) {
    return status;
  }
  *current_voltage = resp.current_voltage;
  return status;
}

zx_status_t FragmentProxy::PowerWritePmicCtrlReg(uint32_t reg_addr, uint32_t value) {
  PowerProxyRequest req = {};
  PowerProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_POWER;
  req.op = PowerOp::WRITE_PMIC_CTRL_REG;
  req.reg_addr = reg_addr;
  req.reg_value = value;

  auto status = Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
  if (status != ZX_OK) {
    return status;
  }
  return status;
}

zx_status_t FragmentProxy::PowerReadPmicCtrlReg(uint32_t reg_addr, uint32_t* out_value) {
  PowerProxyRequest req = {};
  PowerProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_POWER;
  req.op = PowerOp::READ_PMIC_CTRL_REG;
  req.reg_addr = reg_addr;

  auto status = Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
  if (status != ZX_OK) {
    return status;
  }
  *out_value = resp.reg_value;
  return status;
}

zx_status_t FragmentProxy::PwmGetConfig(pwm_config_t* out_config) {
  PwmProxyRequest req = {};
  PwmProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_PWM;
  req.op = PwmOp::GET_CONFIG;
  req.config.mode_config_size = out_config->mode_config_size;

  auto status = Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
  if (status != ZX_OK) {
    return status;
  }
  out_config->polarity = resp.config.polarity;
  out_config->period_ns = resp.config.period_ns;
  out_config->duty_cycle = resp.config.duty_cycle;
  out_config->mode_config_size = resp.config.mode_config_size;
  memcpy(out_config->mode_config_buffer, resp.mode_cfg, resp.config.mode_config_size);
  return status;
}

zx_status_t FragmentProxy::PwmSetConfig(const pwm_config_t* config) {
  PwmProxyRequest req = {};
  PwmProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_PWM;
  req.op = PwmOp::SET_CONFIG;
  req.config = *config;
  memcpy(req.mode_cfg, config->mode_config_buffer, config->mode_config_size);

  return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t FragmentProxy::PwmEnable() {
  PwmProxyRequest req = {};
  PwmProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_PWM;
  req.op = PwmOp::ENABLE;

  return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t FragmentProxy::PwmDisable() {
  PwmProxyRequest req = {};
  PwmProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_PWM;
  req.op = PwmOp::DISABLE;

  return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t FragmentProxy::VregSetVoltageStep(uint32_t step) {
  VregProxyRequest req = {};
  VregProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_VREG;
  req.op = VregOp::SET_VOLTAGE_STEP;
  req.step = step;

  return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
}

uint32_t FragmentProxy::VregGetVoltageStep() {
  VregProxyRequest req = {};
  VregProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_VREG;
  req.op = VregOp::GET_VOLTAGE_STEP;

  Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));

  return resp.step;
}

void FragmentProxy::VregGetRegulatorParams(vreg_params_t* out_params) {
  VregProxyRequest req = {};
  VregProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_VREG;
  req.op = VregOp::GET_REGULATOR_PARAMS;

  auto status = Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
  if (status != ZX_OK) {
    return;
  }

  out_params->min_uv = resp.params.min_uv;
  out_params->step_size_uv = resp.params.step_size_uv;
  out_params->num_steps = resp.params.num_steps;
}

void FragmentProxy::RegistersConnect(zx::channel chan) {
  RegistersProxyRequest req = {};
  RegistersProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_REGISTERS;
  req.op = RegistersOp::CONNECT;

  zx_handle_t handle = chan.release();
  Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), &handle, 1, nullptr, 0, nullptr);
}

void FragmentProxy::RpmbConnectServer(zx::channel server) {
  RpmbProxyRequest req = {};
  RpmbProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_RPMB;
  req.op = RpmbOp::CONNECT_SERVER;

  zx_handle_t channel = server.release();
  Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), &channel, 1, nullptr, 0, nullptr);
}

zx_status_t FragmentProxy::SpiTransmit(const uint8_t* txdata_list, size_t txdata_count) {
  return SpiExchange(txdata_list, txdata_count, NULL, 0, NULL);
}

zx_status_t FragmentProxy::SpiReceive(uint32_t size, uint8_t* out_rxdata_list, size_t rxdata_count,
                                      size_t* out_rxdata_actual) {
  return SpiExchange(NULL, 0, out_rxdata_list, size, out_rxdata_actual);
}
zx_status_t FragmentProxy::SpiExchange(const uint8_t* txdata_list, size_t txdata_count,
                                       uint8_t* out_rxdata_list, size_t rxdata_count,
                                       size_t* out_rxdata_actual) {
  uint8_t req_buffer[kProxyMaxTransferSize];
  auto req = reinterpret_cast<SpiProxyRequest*>(req_buffer);
  req->header.proto_id = ZX_PROTOCOL_SPI;

  if (txdata_count && rxdata_count) {
    req->op = SpiOp::EXCHANGE;
    req->length = txdata_count;
  } else if (txdata_count) {
    req->op = SpiOp::TRANSMIT;
    req->length = txdata_count;
  } else {
    req->op = SpiOp::RECEIVE;
    req->length = rxdata_count;
  }

  size_t req_length = sizeof(SpiProxyRequest) + txdata_count;
  if (req_length >= kProxyMaxTransferSize) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  const size_t resp_length = sizeof(SpiProxyResponse) + rxdata_count;
  if (req_length >= kProxyMaxTransferSize) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  if (txdata_count) {
    uint8_t* p_write = reinterpret_cast<uint8_t*>(&req[1]);
    memcpy(p_write, txdata_list, txdata_count);
  }

  uint8_t resp_buffer[kProxyMaxTransferSize];
  auto resp = reinterpret_cast<SpiProxyResponse*>(resp_buffer);

  size_t actual;
  auto status = Rpc(&req->header, static_cast<uint32_t>(req_length), &resp->header,
                    static_cast<uint32_t>(resp_length), nullptr, 0, nullptr, 0, &actual);
  if (status != ZX_OK) {
    return status;
  }

  if (actual != resp_length) {
    return ZX_ERR_INTERNAL;
  }

  if (rxdata_count) {
    uint8_t* p_read = reinterpret_cast<uint8_t*>(&resp[1]);
    memcpy(out_rxdata_list, p_read, rxdata_count);
    *out_rxdata_actual = rxdata_count;
  }

  return ZX_OK;
}

void FragmentProxy::SpiConnectServer(zx::channel server) {
  SpiProxyRequest req = {};
  req.header.proto_id = ZX_PROTOCOL_SPI;
  req.op = SpiOp::CONNECT_SERVER;

  SpiProxyResponse resp = {};
  zx_handle_t in_handles[] = {server.release()};
  Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), in_handles, countof(in_handles),
      nullptr, 0, nullptr);
}

zx_status_t FragmentProxy::SysmemConnect(zx::channel allocator2_request) {
  SysmemProxyRequest req = {};
  ProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_SYSMEM;
  req.op = SysmemOp::CONNECT;
  zx_handle_t handle = allocator2_request.release();

  return Rpc(&req.header, sizeof(req), &resp, sizeof(resp), &handle, 1, nullptr, 0, nullptr);
}

zx_status_t FragmentProxy::SysmemRegisterHeap(uint64_t heap, zx::channel heap_connection) {
  SysmemProxyRequest req = {};
  ProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_SYSMEM;
  req.op = SysmemOp::REGISTER_HEAP;
  req.heap = heap;
  zx_handle_t handle = heap_connection.release();

  return Rpc(&req.header, sizeof(req), &resp, sizeof(resp), &handle, 1, nullptr, 0, nullptr);
}

zx_status_t FragmentProxy::SysmemRegisterSecureMem(zx::channel secure_mem_connection) {
  SysmemProxyRequest req = {};
  ProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_SYSMEM;
  req.op = SysmemOp::REGISTER_SECURE_MEM;
  zx_handle_t handle = secure_mem_connection.release();

  return Rpc(&req.header, sizeof(req), &resp, sizeof(resp), &handle, 1, nullptr, 0, nullptr);
}

zx_status_t FragmentProxy::SysmemUnregisterSecureMem() {
  SysmemProxyRequest req = {};
  ProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_SYSMEM;
  req.op = SysmemOp::UNREGISTER_SECURE_MEM;

  return Rpc(&req.header, sizeof(req), &resp, sizeof(resp), nullptr, 0, nullptr, 0, nullptr);
}

zx_status_t FragmentProxy::TeeConnectToApplication(const uuid_t* application_uuid,
                                                   zx::channel tee_app_request,
                                                   zx::channel service_provider) {
  TeeProxyRequest req = {};
  ProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_TEE;
  req.op = TeeOp::CONNECT_TO_APPLICATION;
  req.application_uuid = *application_uuid;
  zx_handle_t handles[2] = {tee_app_request.release(), service_provider.release()};

  // service_provider is allowed to be ZX_HANDLE_INVALID
  uint32_t handle_count = 1;
  if (handles[1] != ZX_HANDLE_INVALID) {
    handle_count += 1;
  }

  return Rpc(&req.header, sizeof(req), &resp, sizeof(resp), handles, handle_count, nullptr, 0,
             nullptr);
}

zx_status_t FragmentProxy::UsbModeSwitchSetMode(usb_mode_t mode) {
  UsbModeSwitchProxyRequest req = {};
  ProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_USB_MODE_SWITCH;
  req.op = UsbModeSwitchOp::SET_MODE;
  req.mode = mode;

  return Rpc(&req.header, sizeof(req), &resp, sizeof(resp));
}

zx_status_t FragmentProxy::DsiConnect(zx::channel server) {
  DsiProxyRequest req = {};
  ProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_DSI;
  req.op = DsiOp::CONNECT;
  zx_handle_t handle = server.release();
  return Rpc(&req.header, sizeof(req), &resp, sizeof(resp), &handle, 1, nullptr, 0, nullptr);
}

zx_status_t FragmentProxy::PowerSensorConnectServer(zx::channel server) {
  PowerSensorProxyRequest req = {};
  PowerSensorProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_POWER_SENSOR;
  req.op = PowerSensorOp::CONNECT_SERVER;

  zx_handle_t channel = server.release();
  return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), &channel, 1, nullptr, 0,
             nullptr);
}

void FragmentProxy::AcpiConnectServer(zx::channel server) {
  AcpiProxyRequest req = {};
  AcpiProxyResponse resp = {};
  req.header.proto_id = ZX_PROTOCOL_ACPI;
  req.op = AcpiOp::CONNECT_SERVER;

  zx_handle_t channel = server.release();
  Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), &channel, 1, nullptr, 0, nullptr);
}

const zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.create = FragmentProxy::Create;
  return ops;
}();

}  // namespace fragment

ZIRCON_DRIVER(fragment_proxy, fragment::driver_ops, "zircon", "0.1");
