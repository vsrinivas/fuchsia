// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/test/c/fidl.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/channel.h>
#include <lib/zx/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include <ddk/binding.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <ddktl/protocol/test.h>
#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/string_piece.h>

namespace {

class TestDevice;
using TestDeviceType = ddk::Device<TestDevice, ddk::Messageable, ddk::UnbindableDeprecated>;

class TestDevice : public TestDeviceType, public ddk::TestProtocol<TestDevice, ddk::base_protocol> {
 public:
  TestDevice(zx_device_t* parent) : TestDeviceType(parent) {}

  // Methods required by the ddk mixins
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
  void DdkRelease();
  void DdkUnbindDeprecated();

  // Methods required by the TestProtocol mixin
  void TestSetOutputSocket(zx::socket socket);
  void TestGetOutputSocket(zx::socket* out_socket);
  void TestGetChannel(zx::channel* out_channel);
  void TestSetTestFunc(const test_func_t* func);
  zx_status_t TestRunTests(test_report_t* out_report);
  void TestDestroy();

  void SetChannel(zx::channel c);

 private:
  // Lock that synchronizes calls to DdkRemoveDeprecated()
  fbl::Mutex remove_lock_;
  bool has_been_removed_ TA_GUARDED(remove_lock_) = false;

  zx::socket output_;
  zx::channel channel_;
  test_func_t test_func_;
};

class TestRootDevice;
using TestRootDeviceType = ddk::Device<TestRootDevice, ddk::Messageable, ddk::UnbindableDeprecated>;

class TestRootDevice : public TestRootDeviceType {
 public:
  TestRootDevice(zx_device_t* parent) : TestRootDeviceType(parent) {}

  zx_status_t Bind() { return DdkAdd("test"); }

  // Methods required by the ddk mixins
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
  void DdkRelease() { delete this; }
  void DdkUnbindDeprecated() { DdkRemoveDeprecated(); }

  static zx_status_t FidlCreateDevice(void* ctx, const char* name_data, size_t name_len,
                                      zx_handle_t client_remote_raw, fidl_txn_t* txn);

 private:
  // Create a new child device with this |name|
  zx_status_t CreateDevice(const fbl::StringPiece& name, zx::channel client_remote, char* path_out,
                           size_t path_size, size_t* path_actual);
};

void TestDevice::TestSetOutputSocket(zx::socket socket) { output_ = std::move(socket); }

void TestDevice::TestGetOutputSocket(zx::socket* out_socket) {
  output_.duplicate(ZX_RIGHT_SAME_RIGHTS, out_socket);
}

void TestDevice::TestGetChannel(zx::channel* out_channel) { *out_channel = std::move(channel_); }

void TestDevice::TestSetTestFunc(const test_func_t* func) { test_func_ = *func; }

zx_status_t TestDevice::TestRunTests(test_report_t* report) {
  if (test_func_.callback == NULL) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return test_func_.callback(test_func_.ctx, report);
}

void TestDevice::TestDestroy() {
  fbl::AutoLock guard(&remove_lock_);
  if (!has_been_removed_) {
    output_.reset();
    DdkRemoveDeprecated();
    has_been_removed_ = true;
  }
}

static zx_status_t fidl_SetOutputSocket(void* ctx, zx_handle_t raw_socket) {
  zx::socket socket(raw_socket);
  auto dev = static_cast<TestDevice*>(ctx);
  dev->TestSetOutputSocket(std::move(socket));
  return ZX_OK;
}

void TestDevice::SetChannel(zx::channel c) { channel_ = std::move(c); }

static zx_status_t fidl_SetChannel(void* ctx, zx_handle_t raw_channel) {
  zx::channel channel(raw_channel);
  auto dev = static_cast<TestDevice*>(ctx);
  dev->SetChannel(std::move(channel));
  return ZX_OK;
}

static zx_status_t fidl_RunTests(void* ctx, fidl_txn_t* txn) {
  auto dev = static_cast<TestDevice*>(ctx);
  test_report_t report = {};
  fuchsia_device_test_TestReport fidl_report = {};

  zx_status_t status = dev->TestRunTests(&report);
  if (status == ZX_OK) {
    fidl_report.test_count = report.n_tests;
    fidl_report.success_count = report.n_success;
    fidl_report.failure_count = report.n_failed;
  }
  return fuchsia_device_test_DeviceRunTests_reply(txn, status, &fidl_report);
}

static zx_status_t fidl_Destroy(void* ctx) {
  auto dev = static_cast<TestDevice*>(ctx);
  dev->TestDestroy();
  return ZX_OK;
}

zx_status_t TestDevice::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  static const fuchsia_device_test_Device_ops_t kOps = {
      .RunTests = fidl_RunTests,
      .SetOutputSocket = fidl_SetOutputSocket,
      .SetChannel = fidl_SetChannel,
      .Destroy = fidl_Destroy,
  };
  return fuchsia_device_test_Device_dispatch(this, txn, msg, &kOps);
}

void TestDevice::DdkRelease() { delete this; }

void TestDevice::DdkUnbindDeprecated() { TestDestroy(); }

zx_status_t TestRootDevice::CreateDevice(const fbl::StringPiece& name, zx::channel client_remote,
                                         char* path_out, size_t path_size, size_t* path_actual) {
  static_assert(fuchsia_device_test_MAX_DEVICE_NAME_LEN == ZX_DEVICE_NAME_MAX);

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

  if (path_size < strlen(devname) + sizeof(fuchsia_device_test_CONTROL_DEVICE) + 1) {
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
      snprintf(path_out, path_size, "%s/%s", fuchsia_device_test_CONTROL_DEVICE, devname);
  return ZX_OK;
}

zx_status_t TestRootDevice::FidlCreateDevice(void* ctx, const char* name_data, size_t name_len,
                                             zx_handle_t client_remote_raw, fidl_txn_t* txn) {
  auto root = static_cast<TestRootDevice*>(ctx);
  zx::channel client_remote{client_remote_raw};

  char path[fuchsia_device_test_MAX_DEVICE_PATH_LEN];
  size_t path_size = 0;
  zx_status_t status = root->CreateDevice(fbl::StringPiece(name_data, name_len),
                                          std::move(client_remote), path, sizeof(path), &path_size);
  return fuchsia_device_test_RootDeviceCreateDevice_reply(txn, status, path, path_size);
}

zx_status_t TestRootDevice::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  static const fuchsia_device_test_RootDevice_ops_t kOps = {
      .CreateDevice = TestRootDevice::FidlCreateDevice,
  };
  return fuchsia_device_test_RootDevice_dispatch(this, txn, msg, &kOps);
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

ZIRCON_DRIVER_BEGIN(test, kTestDriverOps, "zircon", "0.1", 4)
BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_TEST_PARENT),

    // This bind functionality is used for the libdriver integration tests to
    // let us hook into composite devices after they are instantiated.
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TEST),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_LIBDRIVER_TEST),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_TEST_COMPOSITE), ZIRCON_DRIVER_END(test)
