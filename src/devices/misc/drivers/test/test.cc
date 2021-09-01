// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.device.test/cpp/wire.h>
#include <fuchsia/hardware/test/cpp/banjo.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/channel.h>
#include <lib/zx/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <string_view>

#include <ddktl/device.h>
#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

#include "src/devices/misc/drivers/test/test_bind.h"

namespace {

class TestDevice;
using TestDeviceType =
    ddk::Device<TestDevice, ddk::Messageable<fuchsia_device_test::Device>::Mixin, ddk::Unbindable>;

class TestDevice : public TestDeviceType, public ddk::TestProtocol<TestDevice, ddk::base_protocol> {
 public:
  TestDevice(zx_device_t* parent) : TestDeviceType(parent) {}

  // Methods required by the ddk mixins
  void DdkRelease();
  void DdkUnbind(ddk::UnbindTxn txn);

  // Methods required by the TestProtocol mixin
  void TestSetOutputSocket(zx::socket socket);
  void TestGetOutputSocket(zx::socket* out_socket);
  void TestGetChannel(zx::channel* out_channel);
  void TestSetTestFunc(const test_func_callback_t* func);
  zx_status_t TestRunTests(test_report_t* out_report);
  void TestDestroy();

  void RunTests(RunTestsRequestView request, RunTestsCompleter::Sync& completer);
  void SetOutputSocket(SetOutputSocketRequestView request,
                       SetOutputSocketCompleter::Sync& completer);
  void SetChannel(SetChannelRequestView request, SetChannelCompleter::Sync& completer);
  void Destroy(DestroyRequestView request, DestroyCompleter::Sync& completer);

 private:
  zx::socket output_;
  zx::channel channel_;
  test_func_callback_t test_func_;
};

class TestRootDevice;
using TestRootDeviceType =
    ddk::Device<TestRootDevice, ddk::Messageable<fuchsia_device_test::RootDevice>::Mixin>;

class TestRootDevice : public TestRootDeviceType {
 public:
  TestRootDevice(zx_device_t* parent) : TestRootDeviceType(parent) {}

  zx_status_t Bind() { return DdkAdd("test"); }

  // Methods required by the ddk mixins
  void DdkRelease() { delete this; }

  void CreateDevice(CreateDeviceRequestView request, CreateDeviceCompleter::Sync& completer);

 private:
  // Create a new child device with this |name|
  zx_status_t CreateDeviceInternal(std::string_view name, zx::channel client_remote, char* path_out,
                                   size_t path_size, size_t* path_actual);
};

void TestDevice::TestSetOutputSocket(zx::socket socket) { output_ = std::move(socket); }

void TestDevice::TestGetOutputSocket(zx::socket* out_socket) {
  output_.duplicate(ZX_RIGHT_SAME_RIGHTS, out_socket);
}

void TestDevice::TestGetChannel(zx::channel* out_channel) { *out_channel = std::move(channel_); }

void TestDevice::TestSetTestFunc(const test_func_callback_t* func) { test_func_ = *func; }

zx_status_t TestDevice::TestRunTests(test_report_t* report) {
  if (test_func_.callback == NULL) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return test_func_.callback(test_func_.ctx, report);
}

void TestDevice::TestDestroy() {
  output_.reset();
  DdkAsyncRemove();
}

void TestDevice::SetOutputSocket(SetOutputSocketRequestView request,
                                 SetOutputSocketCompleter::Sync& completer) {
  TestSetOutputSocket(std::move(request->sock));
}

void TestDevice::SetChannel(SetChannelRequestView request, SetChannelCompleter::Sync& completer) {
  channel_ = std::move(request->chan);
}

void TestDevice::RunTests(RunTestsRequestView request, RunTestsCompleter::Sync& completer) {
  test_report_t report = {};
  fuchsia_device_test::wire::TestReport fidl_report = {};

  zx_status_t status = TestRunTests(&report);
  if (status == ZX_OK) {
    fidl_report.test_count = report.n_tests;
    fidl_report.success_count = report.n_success;
    fidl_report.failure_count = report.n_failed;
  }
  completer.Reply(status, fidl_report);
}

void TestDevice::Destroy(DestroyRequestView request, DestroyCompleter::Sync& completer) {
  TestDestroy();
}

void TestDevice::DdkRelease() { delete this; }

void TestDevice::DdkUnbind(ddk::UnbindTxn txn) {
  TestDestroy();
  txn.Reply();
}

zx_status_t TestRootDevice::CreateDeviceInternal(std::string_view name, zx::channel client_remote,
                                                 char* path_out, size_t path_size,
                                                 size_t* path_actual) {
  static_assert(fuchsia_device_test::wire::kMaxDeviceNameLen == ZX_DEVICE_NAME_MAX);

  char devname[ZX_DEVICE_NAME_MAX + 1] = {};
  if (name.size() > 0) {
    memcpy(devname, name.data(), std::min(sizeof(devname) - 1, name.size()));
  } else {
    strncpy(devname, "testdev", sizeof(devname) - 1);
  }
  devname[sizeof(devname) - 1] = '\0';
  // truncate trailing ".so"
  if (!strcmp(devname + strlen(devname) - 3, ".so")) {
    devname[strlen(devname) - 3] = 0;
  }

  if (path_size < strlen(devname) + strlen(fuchsia_device_test::wire::kControlDevice) + 1) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  auto device = std::make_unique<TestDevice>(zxdev());
  zx_status_t status =
      device->DdkAdd(ddk::DeviceAddArgs(devname).set_client_remote(std::move(client_remote)));
  if (status != ZX_OK) {
    return status;
  }
  // devmgr now owns this
  __UNUSED auto ptr = device.release();

  *path_actual =
      snprintf(path_out, path_size, "%s/%s", fuchsia_device_test::wire::kControlDevice, devname);
  return ZX_OK;
}

void TestRootDevice::CreateDevice(CreateDeviceRequestView request,
                                  CreateDeviceCompleter::Sync& completer) {
  char path[fuchsia_device_test::wire::kMaxDevicePathLen];
  size_t path_size = 0;
  zx_status_t status = CreateDeviceInternal(request->name.get(), std::move(request->device_request),
                                            path, sizeof(path), &path_size);
  completer.Reply(status, fidl::StringView::FromExternal(path, path_size));
}

zx_status_t TestDriverBind(void* ctx, zx_device_t* dev) {
  auto root = std::make_unique<TestRootDevice>(dev);
  zx_status_t status = root->Bind();
  if (status != ZX_OK) {
    return status;
  }
  // devmgr now owns root
  __UNUSED auto ptr = root.release();
  return ZX_OK;
}

const zx_driver_ops_t kTestDriverOps = []() {
  zx_driver_ops_t driver = {};
  driver.version = DRIVER_OPS_VERSION;
  driver.bind = TestDriverBind;
  return driver;
}();

}  // namespace

ZIRCON_DRIVER(test, kTestDriverOps, "zircon", "0.1");
