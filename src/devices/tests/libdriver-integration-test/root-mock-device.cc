// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "root-mock-device.h"

#include <fcntl.h>
#include <fuchsia/device/cpp/fidl.h>
#include <fuchsia/device/test/c/fidl.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/assert.h>

#include <fbl/auto_call.h>

#define DRIVER_TEST_DIR "/boot/driver/test"
#define MOCK_DEVICE_LIB "/boot/driver/test/mock-device.so"

namespace libdriver_integration_test {

RootMockDevice::RootMockDevice(std::unique_ptr<MockDeviceHooks> hooks,
                               fidl::InterfacePtr<fuchsia::device::test::Device> test_device,
                               fidl::InterfaceRequest<fuchsia::device::mock::MockDevice> controller,
                               async_dispatcher_t* dispatcher, std::string path)
    : test_device_(std::move(test_device)),
      path_(std::move(path)),
      mock_(std::move(controller), dispatcher, "") {
  mock_.set_hooks(std::move(hooks));
}

RootMockDevice::~RootMockDevice() {
  // This will trigger unbind() to be called on any device that was added in
  // the bind hook.
  test_device_->Destroy();
}

// |*test_device_out| will be a channel to the test device that the mock device
// bound to.  This is provided so we can trigger unbinding of the mock device.
// |*control_out| will be a channel for fulfilling requests from the mock
// device.
zx_status_t RootMockDevice::Create(const IsolatedDevmgr& devmgr, async_dispatcher_t* dispatcher,
                                   std::unique_ptr<MockDeviceHooks> hooks,
                                   std::unique_ptr<RootMockDevice>* mock_out) {
  // Wait for /dev/test/test to appear
  fbl::unique_fd fd;
  zx_status_t status =
      devmgr_integration_test::RecursiveWaitForFile(devmgr.devfs_root(), "test/test", &fd);
  if (status != ZX_OK) {
    return status;
  }

  zx::channel test_root_chan;
  status = fdio_get_service_handle(fd.release(), test_root_chan.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }

  fidl::SynchronousInterfacePtr<fuchsia::device::test::RootDevice> test_root;
  test_root.Bind(std::move(test_root_chan));

  return CreateFromTestRoot(devmgr, dispatcher, std::move(test_root), std::move(hooks), mock_out);
}

zx_status_t RootMockDevice::CreateFromTestRoot(
    const IsolatedDevmgr& devmgr, async_dispatcher_t* dispatcher,
    fidl::SynchronousInterfacePtr<fuchsia::device::test::RootDevice> test_root,
    std::unique_ptr<MockDeviceHooks> hooks, std::unique_ptr<RootMockDevice>* mock_out) {
  fidl::StringPtr devpath;
  zx_status_t call_status;
  zx_status_t status = test_root->CreateDevice("mock", &call_status, &devpath);
  if (status != ZX_OK) {
    return status;
  }
  if (call_status != ZX_OK) {
    return call_status;
  }

  // Ignore the |devpath| return and construct it ourselves, since the test
  // driver makes an assumption about where it's bound which isn't true in the
  // case where we're testing composite devices
  fidl::SynchronousInterfacePtr<fuchsia::device::Controller> test_root_controller;
  test_root_controller.Bind(test_root.Unbind().TakeChannel());
  fuchsia::device::Controller_GetTopologicalPathNew_Result result;
  status = test_root_controller->GetTopologicalPathNew(&result);
  if (status != ZX_OK) {
    return status;
  }
  if (result.is_err()) {
    return status;
  }
  devpath = result.response().path;
  test_root.Bind(test_root_controller.Unbind().TakeChannel());

  const char* kDevPrefix = "/dev/";
  if (!devpath.has_value() || devpath.value().find(kDevPrefix) != 0) {
    return ZX_ERR_BAD_STATE;
  }
  std::string relative_devpath(devpath.value(), strlen(kDevPrefix));
  relative_devpath += "/mock";
  fbl::unique_fd fd(openat(devmgr.devfs_root().get(), relative_devpath.c_str(), O_RDWR));
  if (!fd.is_valid()) {
    return ZX_ERR_NOT_FOUND;
  }
  zx::channel test_dev;
  status = fdio_get_service_handle(fd.release(), test_dev.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }

  auto destroy_device =
      fbl::MakeAutoCall([&test_dev] { fuchsia_device_test_DeviceDestroy(test_dev.get()); });

  fidl::InterfaceHandle<fuchsia::device::mock::MockDevice> client;
  fidl::InterfaceRequest<fuchsia::device::mock::MockDevice> server(client.NewRequest());
  if (!server.is_valid()) {
    return ZX_ERR_BAD_STATE;
  }

  status = fuchsia_device_test_DeviceSetChannel(test_dev.get(), client.TakeChannel().release());
  if (status != ZX_OK) {
    return status;
  }

  // Open a new connection to the test device to return.  We do to simplify
  // handling around the blocking nature of fuchsia.device.Controller/Bind.  Needs to
  // happen before the bind(), since bind() will cause us to get blocked in the mock device
  // driver waiting for input on what to do.
  zx::channel test_device_channel;
  fbl::unique_fd new_connection(
      openat(devmgr.devfs_root().get(), relative_devpath.c_str(), O_RDWR));
  status = fdio_get_service_handle(new_connection.release(),
                                   test_device_channel.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }
  fidl::InterfacePtr<fuchsia::device::test::Device> test_device;
  status = test_device.Bind(std::move(test_device_channel), dispatcher);
  if (status != ZX_OK) {
    return status;
  }

  // Bind the mock device driver in a separate thread, since this call is
  // synchronous.
  thrd_t thrd;
  int ret = thrd_create(
      &thrd,
      [](void* ctx) {
        zx::channel test_dev(static_cast<zx_handle_t>(reinterpret_cast<uintptr_t>(ctx)));
        fidl::SynchronousInterfacePtr<fuchsia::device::Controller> controller;
        controller.Bind(std::move(test_dev));
        fuchsia::device::Controller_Bind_Result result;
        controller->Bind(MOCK_DEVICE_LIB, &result);
        return 0;
      },
      reinterpret_cast<void*>(static_cast<uintptr_t>(test_dev.release())));
  ZX_ASSERT(ret == thrd_success);
  thrd_detach(thrd);

  destroy_device.cancel();

  auto mock =
      std::make_unique<RootMockDevice>(std::move(hooks), std::move(test_device), std::move(server),
                                       dispatcher, std::move(relative_devpath));
  *mock_out = std::move(mock);
  return ZX_OK;
}

}  // namespace libdriver_integration_test
