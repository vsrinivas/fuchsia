// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/misc/drivers/compat/v1_client_remote_test.h"

#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>

#include "src/devices/misc/drivers/compat/v1_test_bind.h"

namespace v1_client_remote_test {

Device::Device(zx_device_t* parent) : DeviceType(parent) {}

zx_status_t Device::DriverInit(void** out_ctx) {
  *out_ctx = new Context{};
  return ZX_OK;
}

zx_status_t Device::DriverBind(void* ctx_ptr, zx_device_t* dev) {
  const char* kDeviceName = "v1-remote-client-test-device";
  auto v1_dev = std::make_unique<Device>(dev);

  auto endpoints = fidl::CreateEndpoints<fuchsia_test_echo::Echo>();
  if (endpoints.is_error()) {
    zxlogf(ERROR, "Failed to create FIDL endpoints: %s", endpoints.status_string());
    return endpoints.error_value();
  }

  auto status = v1_dev->DdkAdd(ddk::DeviceAddArgs(kDeviceName)
                                   .set_client_remote(std::move(endpoints->server).TakeChannel()));
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to add %s device: %s", kDeviceName, zx_status_get_string(status));
    return status;
  }

  __UNUSED auto* ptr = v1_dev.release();

  auto ctx = static_cast<Context*>(ctx_ptr);
  const std::lock_guard<std::mutex> lock(ctx->lock);
  ctx->echo_client.emplace(std::move(endpoints->client));

  return ZX_OK;
}

void Device::DdkRelease() { delete this; }

void Device::EchoString(EchoStringRequestView request, EchoStringCompleter::Sync& completer) {
  std::string value(request->value.get());
  auto reply = fidl::StringView::FromExternal(value);
  completer.Reply(reply);
}

constexpr zx_driver_ops_t driver_ops = [] {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.init = Device::DriverInit;
  ops.bind = Device::DriverBind;
  return ops;
}();

}  // namespace v1_client_remote_test

ZIRCON_DRIVER(v1_client_remote_test, v1_client_remote_test::driver_ops, "zircon", "0.1");
