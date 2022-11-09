// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "da7219-dfv1.h"

#include <lib/ddk/platform-defs.h>
#include <lib/zx/clock.h>

#include "src/devices/lib/acpi/client.h"
#include "src/media/audio/drivers/codecs/da7219/da7219-bind.h"

namespace audio::da7219 {

Driver::Driver(zx_device_t* parent, std::shared_ptr<Core> core, bool is_input)
    : Base(parent), core_(core), is_input_(is_input) {
  ddk_proto_id_ = ZX_PROTOCOL_CODEC;
}

void Driver::Connect(ConnectRequestView request, ConnectCompleter::Sync& completer) {
  if (server_) {
    completer.Close(ZX_ERR_NO_RESOURCES);  // Only allow one connection.
    return;
  }
  server_ = std::make_unique<Server>(nullptr, core_, is_input_);
  auto on_unbound = [this](fidl::WireServer<fuchsia_hardware_audio::Codec>*, fidl::UnbindInfo info,
                           fidl::ServerEnd<fuchsia_hardware_audio::Codec> server_end) {
    if (info.is_peer_closed()) {
      DA7219_LOG(DEBUG, "Client disconnected");
    } else if (!info.is_user_initiated()) {
      // Do not log canceled cases which happens too often in particular in test cases.
      if (info.status() != ZX_ERR_CANCELED) {
        DA7219_LOG(ERROR, "Client connection unbound: %s", info.status_string());
      }
    }
    if (server_) {
      server_.reset();
    }
  };
  fidl::BindServer<fidl::WireServer<fuchsia_hardware_audio::Codec>>(
      core_->dispatcher(), std::move(request->codec_protocol), server_.get(),
      std::move(on_unbound));
}

zx_status_t Driver::Bind(void* ctx, zx_device_t* parent) {
  auto client = acpi::Client::Create(parent);
  if (!client.is_ok()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto i2c_endpoints = fidl::CreateEndpoints<fuchsia_hardware_i2c::Device>();
  if (i2c_endpoints.is_error()) {
    zxlogf(ERROR, "Failed to create I2C endpoints");
    return i2c_endpoints.error_value();
  }

  zx_status_t status = device_connect_fragment_fidl_protocol(
      parent, "i2c000", fidl::DiscoverableProtocolName<fuchsia_hardware_i2c::Device>,
      i2c_endpoints->server.TakeChannel().release());
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not get i2c protocol");
    return ZX_ERR_NO_RESOURCES;
  }

  auto result = client->borrow()->MapInterrupt(0);
  if (!result.ok() || result.value().is_error()) {
    zxlogf(WARNING, "Could not get IRQ: %s",
           result.ok() ? zx_status_get_string(result.value().error_value())
                       : result.FormatDescription().data());
    return ZX_ERR_NO_RESOURCES;
  }

  // There is a core class that implements the core logic and interaction with the hardware, and a
  // Driver class that allows the creation of multiple instances (one for input and one for output)
  // via multiple DdkAdd invocations.
  // logger is null since it is only used with DFv2.
  auto core = std::make_shared<Core>(nullptr, std::move(i2c_endpoints->client),
                                     std::move(result.value().value()->irq));
  status = core->Initialize();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not initialize");
    return status;
  }

  auto output_driver = std::make_unique<Driver>(parent, core, false);
  zx_device_prop_t output_props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_DIALOG},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_DIALOG_DA7219},
      {BIND_CODEC_INSTANCE, 0, 1},
  };
  status = output_driver->DdkAdd(ddk::DeviceAddArgs("DA7219-output").set_props(output_props));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not add to DDK");
    return status;
  }
  output_driver.release();

  auto input_driver = std::make_unique<Driver>(parent, core, true);
  zx_device_prop_t input_props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_DIALOG},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_DIALOG_DA7219},
      {BIND_CODEC_INSTANCE, 0, 2},
  };
  status = input_driver->DdkAdd(ddk::DeviceAddArgs("DA7219-input").set_props(input_props));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not add to DDK");
    return status;
  }
  input_driver.release();

  return ZX_OK;
}

void Driver::DdkInit(ddk::InitTxn txn) { txn.Reply(ZX_OK); }

void Driver::DdkRelease() { delete this; }

static zx_driver_ops_t driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Driver::Bind;
  return ops;
}();

}  // namespace audio::da7219

ZIRCON_DRIVER(Da7219, audio::da7219::driver_ops, "zircon", "0.1");
