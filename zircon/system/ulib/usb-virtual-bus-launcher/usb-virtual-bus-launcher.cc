// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/usb-virtual-bus-launcher/usb-virtual-bus-launcher.h"

#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/ddk/platform-defs.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/watcher.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/usb-peripheral-utils/event-watcher.h>
#include <lib/usb-virtual-bus-launcher-helper/usb-virtual-bus-launcher-helper.h>
#include <lib/zx/channel.h>
#include <zircon/status.h>

#include <iostream>

#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <usb/usb.h>

namespace usb_virtual {

using driver_integration_test::IsolatedDevmgr;

zx::status<BusLauncher> BusLauncher::Create(IsolatedDevmgr::Args args) {
  args.disable_block_watcher = true;

  board_test::DeviceEntry dev = {};
  dev.did = 0;
  dev.vid = PDEV_VID_TEST;
  dev.pid = PDEV_PID_USB_VBUS_TEST;
  args.device_list.push_back(dev);

  BusLauncher launcher;

  zx_status_t status = IsolatedDevmgr::Create(&args, &launcher.devmgr_);

  fbl::unique_fd fd;
  device_watcher::RecursiveWaitForFile(launcher.devmgr_.devfs_root(),
                                       "sys/platform/11:03:0/usb-virtual-bus", &fd);
  if (!fd.is_valid()) {
    std::cout << "Failed to wait for usb-virtual-bus" << std::endl;
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  zx::channel virtual_bus;
  status = fdio_get_service_handle(fd.release(), virtual_bus.reset_and_get_address());
  if (status != ZX_OK) {
    std::cout << "Failed to get virtual bus service: " << zx_status_get_string(status) << std::endl;
    return zx::error(status);
  }
  launcher.virtual_bus_ =
      fidl::WireSyncClient<fuchsia_hardware_usb_virtual_bus::Bus>(std::move(virtual_bus));

  auto enable_result = launcher.virtual_bus_->Enable();
  if (enable_result.status() != ZX_OK) {
    std::cout << "virtual_bus_->Enable(): " << zx_status_get_string(enable_result.status())
              << std::endl;
    return zx::error(enable_result.status());
  }
  if (enable_result.value().status != ZX_OK) {
    std::cout << "virtual_bus_->Enable() returned status: "
              << zx_status_get_string(enable_result.status()) << std::endl;
    return zx::error(enable_result.value().status);
  }

  fd.reset(openat(launcher.devmgr_.devfs_root().get(), "class/usb-peripheral", O_RDONLY));
  fbl::String devpath;
  while (fdio_watch_directory(fd.get(), usb_virtual_bus::WaitForAnyFile, ZX_TIME_INFINITE,
                              &devpath) != ZX_ERR_STOP)
    continue;

  devpath = fbl::String::Concat({fbl::String("class/usb-peripheral/"), fbl::String(devpath)});
  fd.reset(openat(launcher.devmgr_.devfs_root().get(), devpath.c_str(), O_RDWR));

  zx::channel peripheral;
  status = fdio_get_service_handle(fd.release(), peripheral.reset_and_get_address());
  if (status != ZX_OK) {
    std::cout << "Failed to get USB peripheral service: " << zx_status_get_string(status)
              << std::endl;
    return zx::error(status);
  }
  launcher.peripheral_ =
      fidl::WireSyncClient<fuchsia_hardware_usb_peripheral::Device>(std::move(peripheral));

  status = launcher.ClearPeripheralDeviceFunctions();
  if (status != ZX_OK) {
    std::cout << "launcher.ClearPeripheralDeviceFunctions(): " << zx_status_get_string(status)
              << std::endl;
    return zx::error(status);
  }

  return zx::ok(std::move(launcher));
}

zx_status_t BusLauncher::SetupPeripheralDevice(DeviceDescriptor&& device_desc,
                                               std::vector<ConfigurationDescriptor> config_descs) {
  zx::channel handles[2];
  zx_status_t status = zx::channel::create(0, handles, handles + 1);
  if (status != ZX_OK) {
    std::cout << "Failed to create channel: " << zx_status_get_string(status) << std::endl;
    return status;
  }
  auto set_result = peripheral_->SetStateChangeListener(std::move(handles[1]));
  if (set_result.status() != ZX_OK) {
    std::cout << "peripheral_->SetStateChangeListener(): "
              << zx_status_get_string(set_result.status()) << std::endl;
    return set_result.status();
  }

  auto set_config = peripheral_->SetConfiguration(
      std::move(device_desc),
      fidl::VectorView<ConfigurationDescriptor>::FromExternal(config_descs));
  if (set_config.status() != ZX_OK) {
    std::cout << "peripheral_->SetConfiguration(): " << zx_status_get_string(set_config.status())
              << std::endl;
    return set_config.status();
  }

  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  usb_peripheral_utils::EventWatcher watcher(&loop, std::move(handles[0]), 1);
  loop.Run();
  if (!watcher.all_functions_registered()) {
    std::cout << "watcher.all_functions_registered() returned false" << std::endl;
    return ZX_ERR_INTERNAL;
  }

  auto connect_result = virtual_bus_->Connect();
  if (connect_result.status() != ZX_OK) {
    std::cout << "virtual_bus_->Connect(): " << zx_status_get_string(connect_result.status())
              << std::endl;
    return connect_result.status();
  }
  if (connect_result.value().status != ZX_OK) {
    std::cout << "virtual_bus_->Connect() returned status: "
              << zx_status_get_string(connect_result.value().status) << std::endl;
    return connect_result.value().status;
  }
  return ZX_OK;
}

zx_status_t BusLauncher::ClearPeripheralDeviceFunctions() {
  zx::channel handles[2];
  zx_status_t status = zx::channel::create(0, handles, handles + 1);
  if (status != ZX_OK) {
    std::cout << "Failed to create channel: " << zx_status_get_string(status) << std::endl;
    return status;
  }
  auto set_result = peripheral_->SetStateChangeListener(std::move(handles[1]));
  if (set_result.status() != ZX_OK) {
    std::cout << "peripheral_->SetStateChangeListener(): "
              << zx_status_get_string(set_result.status()) << std::endl;
    return set_result.status();
  }

  auto clear_functions = peripheral_->ClearFunctions();
  if (clear_functions.status() != ZX_OK) {
    std::cout << "peripheral_->ClearFunctions(): " << zx_status_get_string(clear_functions.status())
              << std::endl;
    return clear_functions.status();
  }

  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  usb_peripheral_utils::EventWatcher watcher(&loop, std::move(handles[0]), 1);
  loop.Run();
  if (!watcher.all_functions_cleared()) {
    std::cout << "watcher.all_functions_cleared() returned false" << std::endl;
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

int BusLauncher::GetRootFd() { return devmgr_.devfs_root().get(); }

zx_status_t BusLauncher::Disable() {
  auto result = virtual_bus_->Disable();
  if (result.status() != ZX_OK) {
    std::cout << "virtual_bus_->Disable(): " << zx_status_get_string(result.status()) << std::endl;
    return result.status();
  }
  if (result.value().status != ZX_OK) {
    std::cout << "virtual_bus_->Disable() returned status: "
              << zx_status_get_string(result.value().status) << std::endl;
    return result.value().status;
  }
  return result.value().status;
}

zx_status_t BusLauncher::Disconnect() {
  auto result = virtual_bus_->Disconnect();
  if (result.status() != ZX_OK) {
    std::cout << "virtual_bus_->Disconnect(): " << zx_status_get_string(result.status())
              << std::endl;
    return result.status();
  }
  if (result.value().status != ZX_OK) {
    std::cout << "virtual_bus_->Disconnect() returned status: "
              << zx_status_get_string(result.value().status) << std::endl;
    return result.value().status;
  }
  return ZX_OK;
}

}  // namespace usb_virtual
