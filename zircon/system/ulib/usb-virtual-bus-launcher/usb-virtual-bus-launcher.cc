// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/usb-virtual-bus-launcher/usb-virtual-bus-launcher.h"

#include <ddk/platform-defs.h>
#include <fbl/unique_ptr.h>
#include <fcntl.h>
#include <fuchsia/hardware/usb/virtual/bus/c/fidl.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/spawn.h>
#include <lib/fdio/unsafe.h>
#include <lib/fdio/watcher.h>
#include <lib/fidl-async/bind.h>
#include <lib/fzl/fdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/hw/usb.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zxtest/zxtest.h>

namespace usb_virtual_bus_base {

using driver_integration_test::IsolatedDevmgr;

USBVirtualBusBase::USBVirtualBusBase() {
  zx::channel chn;
  args_.disable_block_watcher = true;
  args_.driver_search_paths.push_back("/boot/driver");
  args_.driver_search_paths.push_back("/boot/driver/test");
  board_test::DeviceEntry dev = {};
  dev.did = 0;
  dev.vid = PDEV_VID_TEST;
  dev.pid = PDEV_PID_USB_VBUS_TEST;
  args_.device_list.push_back(dev);
  zx_status_t status = IsolatedDevmgr::Create(&args_, &devmgr_);
  ASSERT_OK(status);
  fbl::unique_fd fd;
  devmgr_integration_test::RecursiveWaitForFile(devmgr_.devfs_root(),
                                                "sys/platform/11:03:0/usb-virtual-bus", &fd);
  ASSERT_GT(fd.get(), 0);

  fbl::Function<zx_status_t(int, const char*)> callback;
  ASSERT_EQ(ZX_OK,
            fdio_get_service_handle(fd.release(), virtual_bus_handle_.reset_and_get_address()));
  fd = fbl::unique_fd(openat(devmgr_.devfs_root().get(), "class", O_RDONLY));

  ASSERT_EQ(ZX_OK, FidlCall(fuchsia_hardware_usb_virtual_bus_BusEnable, virtual_bus_handle_.get()));
  while (fdio_watch_directory(fd.get(), WaitForFile, ZX_TIME_INFINITE,
                              const_cast<char*>("usb-peripheral")) != ZX_ERR_STOP)
    ;

  fd = fbl::unique_fd(openat(devmgr_.devfs_root().get(), "class/usb-peripheral", O_RDONLY));
  fbl::String devpath;
  while (fdio_watch_directory(fd.get(), WaitForAnyFile, ZX_TIME_INFINITE, &devpath) != ZX_ERR_STOP)
    ;
  devpath = fbl::String::Concat({fbl::String("class/usb-peripheral/"), fbl::String(devpath)});
  fd = fbl::unique_fd(openat(devmgr_.devfs_root().get(), devpath.c_str(), O_RDWR));
  ASSERT_EQ(ZX_OK, fdio_get_service_handle(fd.release(), peripheral_.reset_and_get_address()));
  ASSERT_EQ(ZX_OK,
            FidlCall(fuchsia_hardware_usb_peripheral_DeviceClearFunctions, peripheral_.get()));
}

void USBVirtualBusBase::GetHandles(zx::unowned_channel* peripheral, zx::unowned_channel* bus) {
  *peripheral = zx::unowned_channel(peripheral_);
  *bus = zx::unowned_channel(virtual_bus_handle_);
}

int USBVirtualBusBase::GetRootFd() { return devmgr_.devfs_root().get(); }

}  // namespace usb_virtual_bus_base
