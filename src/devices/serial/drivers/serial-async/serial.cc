// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "serial.h"

#include <fuchsia/hardware/serial/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/ddk/debug.h>
#include <lib/fidl-utils/bind.h>
#include <lib/zx/handle.h>
#include <lib/zx/time.h>
#include <zircon/status.h>
#include <zircon/threads.h>

#include <memory>

#include <fbl/auto_lock.h>
#include <fbl/function.h>

#include "src/devices/serial/drivers/serial-async/serial_bind.h"

namespace serial {

zx_status_t SerialDevice::SerialGetInfo(serial_port_info_t* info) { return serial_.GetInfo(info); }

zx_status_t SerialDevice::SerialConfig(uint32_t baud_rate, uint32_t flags) {
  return serial_.Config(baud_rate, flags);
}

void SerialDevice::GetClass(GetClassRequestView request, GetClassCompleter::Sync& completer) {
  completer.Reply(static_cast<fuchsia_hardware_serial::wire::Class>(serial_class_));
}

void SerialDevice::Read(ReadRequestView request, ReadCompleter::Sync& completer) {
  if (read_completer_.has_value()) {
    completer.ReplyError(ZX_ERR_BAD_STATE);
    return;
  }
  read_completer_ = completer.ToAsync();
  serial_.ReadAsync(
      [](void* ctx, zx_status_t status, const uint8_t* buffer, size_t length) {
        if (status) {
          auto completer = std::move(static_cast<SerialDevice*>(ctx)->read_completer_);
          static_cast<SerialDevice*>(ctx)->read_completer_.reset();
          completer->ReplyError(status);
        } else {
          auto view = fidl::VectorView<uint8_t>::FromExternal(const_cast<uint8_t*>(buffer), length);
          auto completer = std::move(static_cast<SerialDevice*>(ctx)->read_completer_);
          static_cast<SerialDevice*>(ctx)->read_completer_.reset();
          completer->ReplySuccess(std::move(view));
        }
      },
      this);
}

void SerialDevice::GetChannel(GetChannelRequestView request, GetChannelCompleter::Sync& completer) {
  if (loop_.has_value()) {
    if (loop_->GetState() == ASYNC_LOOP_SHUTDOWN) {
      loop_.reset();
    } else {
      completer.Close(ZX_ERR_BAD_STATE);
      return;
    }
  }
  loop_.emplace(&kAsyncLoopConfigNoAttachToCurrentThread);
  loop_->StartThread("serial-thread");

  // Invoked when the channel is closed or on any binding-related error.
  fidl::OnUnboundFn<fidl::WireServer<fuchsia_hardware_serial::NewDevice>> unbound_fn(
      [](fidl::WireServer<fuchsia_hardware_serial::NewDevice>* dev, fidl::UnbindInfo,
         fidl::ServerEnd<fuchsia_hardware_serial::NewDevice>) {
        auto* device = static_cast<SerialDevice*>(dev);
        device->loop_->Quit();
        // Unblock DdkRelease() if it was invoked.
        sync_completion_signal(&device->on_unbind_);
      });

  auto binding =
      fidl::BindServer(loop_->dispatcher(), std::move(request->req),
                       static_cast<fidl::WireServer<fuchsia_hardware_serial::NewDevice>*>(this),
                       std::move(unbound_fn));
  binding_.emplace(std::move(binding));
}

void SerialDevice::Write(WriteRequestView request, WriteCompleter::Sync& completer) {
  if (write_completer_.has_value()) {
    completer.ReplyError(ZX_ERR_BAD_STATE);
    return;
  }
  write_completer_ = completer.ToAsync();
  serial_.WriteAsync(
      request->data.data(), request->data.count(),
      [](void* ctx, zx_status_t status) {
        if (status) {
          auto completer = std::move(static_cast<SerialDevice*>(ctx)->write_completer_);
          static_cast<SerialDevice*>(ctx)->write_completer_.reset();
          completer->ReplyError(status);
        } else {
          auto completer = std::move(static_cast<SerialDevice*>(ctx)->write_completer_);
          static_cast<SerialDevice*>(ctx)->write_completer_.reset();
          completer->ReplySuccess();
        }
      },
      this);
}

void SerialDevice::SetConfig(SetConfigRequestView request, SetConfigCompleter::Sync& completer) {
  using fuchsia_hardware_serial::wire::CharacterWidth;
  using fuchsia_hardware_serial::wire::FlowControl;
  using fuchsia_hardware_serial::wire::Parity;
  using fuchsia_hardware_serial::wire::StopWidth;
  uint32_t flags = 0;
  switch (request->config.character_width) {
    case CharacterWidth::kBits5:
      flags |= SERIAL_DATA_BITS_5;
      break;
    case CharacterWidth::kBits6:
      flags |= SERIAL_DATA_BITS_6;
      break;
    case CharacterWidth::kBits7:
      flags |= SERIAL_DATA_BITS_7;
      break;
    case CharacterWidth::kBits8:
      flags |= SERIAL_DATA_BITS_8;
      break;
  }

  switch (request->config.stop_width) {
    case StopWidth::kBits1:
      flags |= SERIAL_STOP_BITS_1;
      break;
    case StopWidth::kBits2:
      flags |= SERIAL_STOP_BITS_2;
      break;
  }

  switch (request->config.parity) {
    case Parity::kNone:
      flags |= SERIAL_PARITY_NONE;
      break;
    case Parity::kEven:
      flags |= SERIAL_PARITY_EVEN;
      break;
    case Parity::kOdd:
      flags |= SERIAL_PARITY_ODD;
      break;
  }

  switch (request->config.control_flow) {
    case FlowControl::kNone:
      flags |= SERIAL_FLOW_CTRL_NONE;
      break;
    case FlowControl::kCtsRts:
      flags |= SERIAL_FLOW_CTRL_CTS_RTS;
      break;
  }

  zx_status_t status = SerialConfig(request->config.baud_rate, flags);
  completer.Reply(status);
}

zx_status_t SerialDevice::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  fidl::WireDispatch<fuchsia_hardware_serial::NewDeviceProxy>(this, msg, &transaction);
  return transaction.Status();
}

void SerialDevice::DdkRelease() {
  serial_.Enable(false);
  if (binding_) {
    binding_->Unbind();
    sync_completion_wait(&on_unbind_, ZX_TIME_INFINITE);
  }
  delete this;
}

zx_status_t SerialDevice::Create(void* ctx, zx_device_t* dev) {
  std::unique_ptr<SerialDevice> sdev = std::make_unique<SerialDevice>(dev);

  zx_status_t status;
  if ((status = sdev->Init()) != ZX_OK) {
    return status;
  }

  if ((status = sdev->Bind()) != ZX_OK) {
    zxlogf(ERROR, "SerialDevice::Create: Bind failed");
    sdev.release()->DdkRelease();
    return status;
  }

  // devmgr is now in charge of the device.
  __UNUSED auto* dummy = sdev.release();

  return ZX_OK;
}

zx_status_t SerialDevice::Init() {
  if (!serial_.is_valid()) {
    zxlogf(ERROR, "SerialDevice::Init: ZX_PROTOCOL_SERIAL_IMPL_ASYNC not available");
    return ZX_ERR_NOT_SUPPORTED;
  }

  serial_port_info_t info;
  zx_status_t status = serial_.GetInfo(&info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "SerialDevice::Init: SerialImpl::GetInfo failed %d", status);
    return status;
  }
  serial_class_ = info.serial_class;

  return ZX_OK;
}

zx_status_t SerialDevice::Bind() {
  zx_device_prop_t props[] = {
      {BIND_PROTOCOL, 0, ZX_PROTOCOL_SERIAL},
      {BIND_SERIAL_CLASS, 0, serial_class_},
  };

  return DdkAdd(ddk::DeviceAddArgs("serial-async").set_props(props));
}

static constexpr zx_driver_ops_t serial_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = SerialDevice::Create;
  return ops;
}();

}  // namespace serial

// The formatter does not play nice with these macros.
// clang-format off
ZIRCON_DRIVER(serial, serial::serial_driver_ops, "zircon", "*0.1");

// clang-format on
