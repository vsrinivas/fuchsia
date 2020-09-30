// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-virtual-bus.h"

#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/spawn.h>
#include <lib/fdio/unsafe.h>
#include <lib/fdio/watcher.h>
#include <lib/fidl-async/cpp/bind.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/device/vfs.h>
#include <zircon/hw/usb.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <gtest/gtest.h>

namespace usb_virtual_bus {

USBVirtualBusBase::USBVirtualBusBase(std::string pkg_url, std::string svc_name) {
  // create a IsolatedDevmgr through test component
  auto ctx = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  fidl::InterfacePtr<fuchsia::sys::Launcher> launcher;
  ctx->svc()->Connect(launcher.NewRequest());

  zx::channel req;
  services_ = sys::ServiceDirectory::CreateWithRequest(&req);

  fuchsia::sys::LaunchInfo info;
  info.directory_request = std::move(req);
  info.url = pkg_url;
  launcher->CreateComponent(std::move(info), ctlr_.NewRequest());
  ctlr_.set_error_handler([](zx_status_t err) { FAIL() << "Controller shouldn't exit"; });

  zx::channel devfs_server_end, devfs_client_end;
  zx::channel::create(0, &devfs_server_end, &devfs_client_end);
  services_->Connect(svc_name, std::move(devfs_server_end));
  int fd_devfs;
  fdio_fd_create(devfs_client_end.release(), &fd_devfs);
  devfs_.reset(fd_devfs);
}

void USBVirtualBusBase::InitPeripheral() {
  fbl::unique_fd fd;
  devmgr_integration_test::RecursiveWaitForFile(devfs_root(),
                                                "sys/platform/11:03:0/usb-virtual-bus", &fd);
  ASSERT_EQ(fd.is_valid(), true);
  zx::channel virtual_bus;
  ASSERT_EQ(fdio_get_service_handle(fd.release(), virtual_bus.reset_and_get_address()), ZX_OK);
  virtual_bus_.emplace(std::move(virtual_bus));

  auto enable_result = virtual_bus_->Enable();
  ASSERT_EQ(enable_result.status(), ZX_OK);
  ASSERT_EQ(enable_result.value().status, ZX_OK);

  char peripheral_str[] = "usb-peripheral";
  fd.reset(openat(devfs_root().get(), "class", O_RDONLY));
  while (fdio_watch_directory(fd.get(), usb_virtual_bus_helper::WaitForFile, ZX_TIME_INFINITE,
                              &peripheral_str) != ZX_ERR_STOP)
    continue;

  fd.reset(openat(devfs_root().get(), "class/usb-peripheral", O_RDONLY));
  fbl::String devpath;
  while (fdio_watch_directory(fd.get(), usb_virtual_bus_helper::WaitForAnyFile, ZX_TIME_INFINITE,
                              &devpath) != ZX_ERR_STOP)
    continue;

  devpath = fbl::String::Concat({fbl::String("class/usb-peripheral/"), fbl::String(devpath)});
  fd.reset(openat(devfs_root().get(), devpath.c_str(), O_RDWR));

  zx::channel peripheral;
  ASSERT_EQ(fdio_get_service_handle(fd.release(), peripheral.reset_and_get_address()), ZX_OK);
  peripheral_.emplace(std::move(peripheral));

  ASSERT_NO_FATAL_FAILURE(ClearPeripheralDeviceFunctions());
}

int USBVirtualBusBase::GetRootFd() { return devfs_root().get(); }

class EventWatcher : public ::llcpp::fuchsia::hardware::usb::peripheral::Events::Interface {
 public:
  explicit EventWatcher(async::Loop* loop, zx::channel svc, size_t functions)
      : loop_(loop), functions_(functions) {
    fidl::BindSingleInFlightOnly(loop->dispatcher(), std::move(svc), this);
  }

  void FunctionRegistered(FunctionRegisteredCompleter::Sync completer);
  void FunctionsCleared(FunctionsClearedCompleter::Sync completer);

  bool all_functions_registered() { return functions_registered_ == functions_; }
  bool all_functions_cleared() { return all_functions_cleared_; }

 private:
  async::Loop* loop_;
  const size_t functions_;
  size_t functions_registered_ = 0;

  bool all_functions_cleared_ = false;
};

void EventWatcher::FunctionRegistered(FunctionRegisteredCompleter::Sync completer) {
  functions_registered_++;
  if (all_functions_registered()) {
    loop_->Quit();
    completer.Close(ZX_ERR_CANCELED);
  } else {
    completer.Reply();
  }
}

void EventWatcher::FunctionsCleared(FunctionsClearedCompleter::Sync completer) {
  all_functions_cleared_ = true;
  loop_->Quit();
  completer.Close(ZX_ERR_CANCELED);
}

void USBVirtualBusBase::SetupPeripheralDevice(DeviceDescriptor&& device_desc,
                                              std::vector<FunctionDescriptor> function_descs) {
  using ConfigurationDescriptor =
      ::fidl::VectorView<::llcpp::fuchsia::hardware::usb::peripheral::FunctionDescriptor>;
  zx::channel state_change_sender, state_change_receiver;
  ASSERT_EQ(zx::channel::create(0, &state_change_sender, &state_change_receiver), ZX_OK);
  auto set_result = peripheral_->SetStateChangeListener(std::move(state_change_receiver));
  ASSERT_EQ(set_result.status(), ZX_OK);
  std::vector<ConfigurationDescriptor> config_descs;
  config_descs.emplace_back(fidl::unowned_vec(function_descs));
  auto set_config =
      peripheral_->SetConfiguration(std::move(device_desc), ::fidl::unowned_vec(config_descs));
  ASSERT_EQ(set_config.status(), ZX_OK);
  ASSERT_FALSE(set_config->result.is_err());

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  EventWatcher watcher(&loop, std::move(state_change_sender), function_descs.size());
  loop.Run();
  ASSERT_TRUE(watcher.all_functions_registered());

  auto connect_result = virtual_bus_->Connect();
  ASSERT_EQ(connect_result.status(), ZX_OK);
  ASSERT_EQ(connect_result.value().status, ZX_OK);
}

void USBVirtualBusBase::ClearPeripheralDeviceFunctions() {
  zx::channel handles[2];
  ASSERT_EQ(zx::channel::create(0, handles, handles + 1), ZX_OK);
  auto set_result = peripheral_->SetStateChangeListener(std::move(handles[1]));
  ASSERT_EQ(set_result.status(), ZX_OK);

  auto clear_functions = peripheral_->ClearFunctions();
  ASSERT_EQ(clear_functions.status(), ZX_OK);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  EventWatcher watcher(&loop, std::move(handles[0]), 0);
  loop.Run();
  ASSERT_TRUE(watcher.all_functions_cleared());
}

}  // namespace usb_virtual_bus
