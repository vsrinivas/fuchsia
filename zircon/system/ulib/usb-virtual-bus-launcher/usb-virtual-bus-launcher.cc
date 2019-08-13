// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/usb-virtual-bus-launcher/usb-virtual-bus-launcher.h"

#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/spawn.h>
#include <lib/fdio/unsafe.h>
#include <lib/fdio/watcher.h>
#include <lib/fidl-async/cpp/bind.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/hw/usb.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <ddk/platform-defs.h>
#include <fbl/unique_ptr.h>
#include <zxtest/zxtest.h>

namespace usb_virtual_bus_base {

using driver_integration_test::IsolatedDevmgr;
using usb_virtual_bus::ValidateResult;

USBVirtualBusBase::USBVirtualBusBase() {
  args_.disable_block_watcher = true;
  args_.driver_search_paths.push_back("/boot/driver");
  args_.driver_search_paths.push_back("/boot/driver/test");

  board_test::DeviceEntry dev = {};
  dev.did = 0;
  dev.vid = PDEV_VID_TEST;
  dev.pid = PDEV_PID_USB_VBUS_TEST;
  args_.device_list.push_back(dev);

  ASSERT_OK(IsolatedDevmgr::Create(&args_, &devmgr_));

  fbl::unique_fd fd;
  devmgr_integration_test::RecursiveWaitForFile(devmgr_.devfs_root(),
                                                "sys/platform/11:03:0/usb-virtual-bus", &fd);
  ASSERT_TRUE(fd.is_valid());

  zx::channel virtual_bus;
  ASSERT_OK(fdio_get_service_handle(fd.release(), virtual_bus.reset_and_get_address()));
  virtual_bus_.emplace(std::move(virtual_bus));

  auto enable_result = virtual_bus_->Enable();
  ASSERT_NO_FATAL_FAILURES(ValidateResult(enable_result));

  char peripheral_str[] = "usb-peripheral";
  fd.reset(openat(devmgr_.devfs_root().get(), "class", O_RDONLY));
  while (fdio_watch_directory(fd.get(), usb_virtual_bus::WaitForFile, ZX_TIME_INFINITE,
                              &peripheral_str) != ZX_ERR_STOP)
    continue;

  fd.reset(openat(devmgr_.devfs_root().get(), "class/usb-peripheral", O_RDONLY));
  fbl::String devpath;
  while (fdio_watch_directory(fd.get(), usb_virtual_bus::WaitForAnyFile, ZX_TIME_INFINITE,
                              &devpath) != ZX_ERR_STOP)
    continue;

  devpath = fbl::String::Concat({fbl::String("class/usb-peripheral/"), fbl::String(devpath)});
  fd.reset(openat(devmgr_.devfs_root().get(), devpath.c_str(), O_RDWR));

  zx::channel peripheral;
  ASSERT_OK(fdio_get_service_handle(fd.release(), peripheral.reset_and_get_address()));
  peripheral_.emplace(std::move(peripheral));

  auto clear_result = peripheral_->ClearFunctions();
  ASSERT_OK(clear_result.status());
  ASSERT_FALSE(clear_result->result.is_err());
}

int USBVirtualBusBase::GetRootFd() { return devmgr_.devfs_root().get(); }

class EventWatcher : public ::llcpp::fuchsia::hardware::usb::peripheral::Events::Interface {
 public:
  explicit EventWatcher(async::Loop* loop, zx::channel svc, size_t functions)
      : loop_(loop), functions_(functions) {
    fidl::Bind(loop->dispatcher(), std::move(svc), this);
  }

  void FunctionRegistered(FunctionRegisteredCompleter::Sync completer);

  bool all_functions_registered() { return functions_registered_ == functions_; }

 private:
  async::Loop* loop_;
  const size_t functions_;
  size_t functions_registered_ = 0;

  bool state_changed_ = false;
};

void EventWatcher::FunctionRegistered(FunctionRegisteredCompleter::Sync completer) {
  functions_registered_++;
  if (all_functions_registered()) {
    state_changed_ = true;
    loop_->Quit();
    completer.Close(ZX_ERR_CANCELED);
  } else {
    completer.Reply();
  }
}

void USBVirtualBusBase::SetupPeripheralDevice(const DeviceDescriptor& device_desc,
                                              std::vector<FunctionDescriptor> function_descs) {
  zx::channel handles[2];
  ASSERT_OK(zx::channel::create(0, handles, handles + 1));
  auto set_result = peripheral_->SetStateChangeListener(std::move(handles[1]));
  ASSERT_OK(set_result.status());

  auto set_config = peripheral_->SetConfiguration(
      device_desc, ::fidl::VectorView(function_descs.size(), function_descs.data()));
  ASSERT_OK(set_config.status());
  ASSERT_FALSE(set_config->result.is_err());

  async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
  EventWatcher watcher(&loop, std::move(handles[0]), function_descs.size());
  loop.Run();
  ASSERT_TRUE(watcher.all_functions_registered());

  auto connect_result = virtual_bus_->Connect();
  ASSERT_NO_FATAL_FAILURES(ValidateResult(connect_result));
}

}  // namespace usb_virtual_bus_base
