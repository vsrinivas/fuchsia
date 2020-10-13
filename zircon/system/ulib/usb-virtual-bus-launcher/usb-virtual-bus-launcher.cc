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
#include <lib/usb-peripheral-utils/event-watcher.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/hw/usb.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <ddk/platform-defs.h>
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

  ASSERT_NO_FATAL_FAILURES(ClearPeripheralDeviceFunctions());
}

int USBVirtualBusBase::GetRootFd() { return devmgr_.devfs_root().get(); }

void USBVirtualBusBase::SetupPeripheralDevice(DeviceDescriptor&& device_desc,
                                              std::vector<ConfigurationDescriptor> config_descs) {
  zx::channel handles[2];
  ASSERT_OK(zx::channel::create(0, handles, handles + 1));
  auto set_result = peripheral_->SetStateChangeListener(std::move(handles[1]));
  ASSERT_OK(set_result.status());

  auto set_config =
      peripheral_->SetConfiguration(std::move(device_desc), fidl::unowned_vec(config_descs));
  ASSERT_OK(set_config.status());
  ASSERT_FALSE(set_config->result.is_err());

  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  usb_peripheral_utils::EventWatcher watcher(&loop, std::move(handles[0]), 1);
  loop.Run();
  ASSERT_TRUE(watcher.all_functions_registered());

  auto connect_result = virtual_bus_->Connect();
  ASSERT_NO_FATAL_FAILURES(ValidateResult(connect_result));
}

void USBVirtualBusBase::ClearPeripheralDeviceFunctions() {
  zx::channel handles[2];
  ASSERT_OK(zx::channel::create(0, handles, handles + 1));
  auto set_result = peripheral_->SetStateChangeListener(std::move(handles[1]));
  ASSERT_OK(set_result.status());

  auto clear_functions = peripheral_->ClearFunctions();
  ASSERT_OK(clear_functions.status());

  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  usb_peripheral_utils::EventWatcher watcher(&loop, std::move(handles[0]), 1);
  loop.Run();
  ASSERT_TRUE(watcher.all_functions_cleared());
}

}  // namespace usb_virtual_bus_base
