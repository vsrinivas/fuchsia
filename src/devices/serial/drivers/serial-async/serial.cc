// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "serial.h"

#include <fuchsia/hardware/serial/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/async_bind.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl-utils/bind.h>
#include <lib/zx/handle.h>
#include <lib/zx/time.h>
#include <zircon/status.h>
#include <zircon/threads.h>

#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <fbl/auto_lock.h>
#include <fbl/function.h>

namespace serial {

namespace fuchsia = ::llcpp::fuchsia;

zx_status_t SerialDevice::SerialGetInfo(serial_port_info_t* info) { return serial_.GetInfo(info); }

zx_status_t SerialDevice::SerialConfig(uint32_t baud_rate, uint32_t flags) {
  return serial_.Config(baud_rate, flags);
}

void SerialDevice::GetClass(GetClassCompleter::Sync completer) {
  completer.Reply(static_cast<fuchsia::hardware::serial::Class>(serial_class_));
}

void SerialDevice::Read(ReadCompleter::Sync completer) {
  if (read_completer_.has_value()) {
    completer.ReplyError(ZX_ERR_BAD_STATE);
    return;
  }
  read_completer_ = completer.ToAsync();
  serial_.ReadAsync(
      [](void* ctx, zx_status_t status, const void* buffer, size_t length) {
        if (status) {
          auto completer = std::move(static_cast<SerialDevice*>(ctx)->read_completer_);
          static_cast<SerialDevice*>(ctx)->read_completer_.reset();
          completer->ReplyError(status);
        } else {
          auto view = fidl::VectorView<uint8_t>(
              fidl::unowned_ptr(const_cast<uint8_t*>(static_cast<const uint8_t*>(buffer))), length);
          auto completer = std::move(static_cast<SerialDevice*>(ctx)->read_completer_);
          static_cast<SerialDevice*>(ctx)->read_completer_.reset();
          completer->ReplySuccess(std::move(view));
        }
      },
      this);
}

void SerialDevice::GetChannel(zx::channel req, GetChannelCompleter::Sync completer) {
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
  fidl::OnUnboundFn<llcpp::fuchsia::hardware::serial::NewDevice::Interface> unbound_fn(
      [](llcpp::fuchsia::hardware::serial::NewDevice::Interface* dev, fidl::UnboundReason,
         zx_status_t, zx::channel) { static_cast<SerialDevice*>(dev)->loop_->Quit(); });

  auto binding_ref =
      fidl::AsyncBind(loop_->dispatcher(), std::move(req),
                      static_cast<llcpp::fuchsia::hardware::serial::NewDevice::Interface*>(this),
                      std::move(unbound_fn));
  if (binding_ref.is_error()) {
    loop_.reset();
    return;
  }
}

void SerialDevice::Write(fidl::VectorView<uint8_t> data, WriteCompleter::Sync completer) {
  if (write_completer_.has_value()) {
    completer.ReplyError(ZX_ERR_BAD_STATE);
    return;
  }
  write_completer_ = completer.ToAsync();
  serial_.WriteAsync(
      data.data(), data.count(),
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

void SerialDevice::SetConfig(fuchsia::hardware::serial::Config config,
                             SetConfigCompleter::Sync completer) {
  using fuchsia::hardware::serial::CharacterWidth;
  using fuchsia::hardware::serial::FlowControl;
  using fuchsia::hardware::serial::Parity;
  using fuchsia::hardware::serial::StopWidth;
  uint32_t flags = 0;
  switch (config.character_width) {
    case CharacterWidth::BITS_5:
      flags |= SERIAL_DATA_BITS_5;
      break;
    case CharacterWidth::BITS_6:
      flags |= SERIAL_DATA_BITS_6;
      break;
    case CharacterWidth::BITS_7:
      flags |= SERIAL_DATA_BITS_7;
      break;
    case CharacterWidth::BITS_8:
      flags |= SERIAL_DATA_BITS_8;
      break;
  }

  switch (config.stop_width) {
    case StopWidth::BITS_1:
      flags |= SERIAL_STOP_BITS_1;
      break;
    case StopWidth::BITS_2:
      flags |= SERIAL_STOP_BITS_2;
      break;
  }

  switch (config.parity) {
    case Parity::NONE:
      flags |= SERIAL_PARITY_NONE;
      break;
    case Parity::EVEN:
      flags |= SERIAL_PARITY_EVEN;
      break;
    case Parity::ODD:
      flags |= SERIAL_PARITY_ODD;
      break;
  }

  switch (config.control_flow) {
    case FlowControl::NONE:
      flags |= SERIAL_FLOW_CTRL_NONE;
      break;
    case FlowControl::CTS_RTS:
      flags |= SERIAL_FLOW_CTRL_CTS_RTS;
      break;
  }

  zx_status_t status = SerialConfig(config.baud_rate, flags);
  completer.Reply(status);
}

zx_status_t SerialDevice::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  fuchsia::hardware::serial::NewDeviceProxy::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

void SerialDevice::DdkRelease() {
  serial_.Enable(false);
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

  return DdkAdd("serial-async", 0, props, fbl::count_of(props));
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
ZIRCON_DRIVER_BEGIN(serial, serial::serial_driver_ops, "zircon", "*0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_SERIAL_IMPL_ASYNC),
ZIRCON_DRIVER_END(serial)
    // clang-format on
