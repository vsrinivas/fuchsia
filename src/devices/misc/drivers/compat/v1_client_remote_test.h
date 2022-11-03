// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_MISC_DRIVERS_COMPAT_V1_CLIENT_REMOTE_TEST_H_
#define SRC_DEVICES_MISC_DRIVERS_COMPAT_V1_CLIENT_REMOTE_TEST_H_

#include <fidl/fuchsia.test.echo/cpp/fidl.h>
#include <lib/ddk/driver.h>
#include <zircon/types.h>

#include <mutex>
#include <optional>

#include <ddktl/device.h>

namespace v1_client_remote_test {

struct Context {
  // The driver dispatcher is on a separate thread from the test thread,
  // so this is needed for proper synchronization.
  // TODO(fxbug.dev/103368): Fix test framework synchronization.
  std::mutex lock;

  std::optional<fidl::ClientEnd<fuchsia_test_echo::Echo>> echo_client;
  zx_status_t status = ZX_OK;
};

class Device;
using DeviceType = ddk::Device<Device, ddk::Messageable<fuchsia_test_echo::Echo>::Mixin>;

class Device : public DeviceType {
 public:
  static zx_status_t DriverInit(void** out_ctx);
  static zx_status_t DriverBind(void* ctx_ptr, zx_device_t* dev);

  explicit Device(zx_device_t* parent);

  // Device protocol implementation.
  void DdkRelease();

  // |fidl::WireServer<fuchsia_test_echo::Echo>|
  void EchoString(EchoStringRequestView request, EchoStringCompleter::Sync& completer) override;
};

}  // namespace v1_client_remote_test

#endif  // SRC_DEVICES_MISC_DRIVERS_COMPAT_V1_CLIENT_REMOTE_TEST_H_
