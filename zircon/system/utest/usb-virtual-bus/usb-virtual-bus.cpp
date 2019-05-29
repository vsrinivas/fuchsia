// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-virtual-bus.h"
#include "helper.h"

#include <ddk/platform-defs.h>
#include <fbl/unique_ptr.h>
#include <fcntl.h>
#include <fuchsia/usb/virtualbus/c/fidl.h>
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

namespace usb_virtual_bus {

using driver_integration_test::IsolatedDevmgr;

USBVirtualBus::USBVirtualBus() {
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
            "sys/platform/11:03:0/usb-virtual-bus",
            zx::time::infinite(), &fd);
    ASSERT_GT(fd.get(), 0);

    fbl::Function<zx_status_t(int, const char*)> callback;
    ASSERT_EQ(ZX_OK, fdio_get_service_handle(fd.release(),
                virtual_bus_handle_.reset_and_get_address()));
    fd = fbl::unique_fd(openat(devmgr_.devfs_root().get(), "class", O_RDONLY));

    ASSERT_EQ(ZX_OK, FidlCall(fuchsia_usb_virtualbus_BusEnable, virtual_bus_handle_.get()));
    while (fdio_watch_directory(fd.get(), WaitForFile, ZX_TIME_INFINITE,
                const_cast<char*>("usb-peripheral")) != ZX_ERR_STOP)
        ;

    fd = fbl::unique_fd(openat(devmgr_.devfs_root().get(), "class/usb-peripheral", O_RDONLY));
    fbl::String devpath;
    while (fdio_watch_directory(fd.get(), WaitForAnyFile, ZX_TIME_INFINITE, &devpath) !=
            ZX_ERR_STOP)
        ;
    devpath = fbl::String::Concat({fbl::String("class/usb-peripheral/"), fbl::String(devpath)});
    fd = fbl::unique_fd(openat(devmgr_.devfs_root().get(), devpath.c_str(), O_RDWR));
    ASSERT_EQ(ZX_OK,
            fdio_get_service_handle(fd.release(), peripheral_.reset_and_get_address()));
    ASSERT_EQ(ZX_OK, FidlCall(fuchsia_hardware_usb_peripheral_DeviceClearFunctions,
                peripheral_.get()));
}

// Initialize UMS. Asserts on failure.
void USBVirtualBus::InitUMS(fbl::String* devpath) {
    fuchsia_hardware_usb_peripheral_DeviceDescriptor device_desc = {};
    device_desc.bcdUSB = htole16(0x0200);
    device_desc.bDeviceClass = 0;
    device_desc.bDeviceSubClass = 0;
    device_desc.bDeviceProtocol = 0;
    device_desc.bMaxPacketSize0 = 64;
    // idVendor and idProduct are filled in later
    device_desc.bcdDevice = htole16(0x0100);
    // iManufacturer; iProduct and iSerialNumber are filled in later
    device_desc.bNumConfigurations = 1;
    ASSERT_EQ(ZX_OK, AllocateString(peripheral_, "Google", &device_desc.iManufacturer));
    ASSERT_EQ(ZX_OK, AllocateString(peripheral_, "USB test drive", &device_desc.iProduct));
    ASSERT_EQ(ZX_OK, AllocateString(peripheral_, "ebfd5ad49d2a", &device_desc.iSerialNumber));
    device_desc.idVendor = htole16(0x18D1);
    device_desc.idProduct = htole16(0xA021);
    ASSERT_EQ(ZX_OK, FidlCall(fuchsia_hardware_usb_peripheral_DeviceSetDeviceDescriptor,
                peripheral_.get(), &device_desc));

    fuchsia_hardware_usb_peripheral_FunctionDescriptor ums_function_desc = {
        .interface_class = USB_CLASS_MSC,
        .interface_subclass = USB_SUBCLASS_MSC_SCSI,
        .interface_protocol = USB_PROTOCOL_MSC_BULK_ONLY,
    };

    ASSERT_EQ(ZX_OK, FidlCall(fuchsia_hardware_usb_peripheral_DeviceAddFunction,
                peripheral_.get(), &ums_function_desc));
    zx::channel handles[2];
    ASSERT_EQ(ZX_OK, zx::channel::create(0, handles, handles + 1));
    ASSERT_EQ(ZX_OK, fuchsia_hardware_usb_peripheral_DeviceSetStateChangeListener(
                peripheral_.get(), handles[1].get()));
    ASSERT_EQ(ZX_OK,
            FidlCall(fuchsia_hardware_usb_peripheral_DeviceBindFunctions, peripheral_.get()));
    async_loop_config_t config = {};
    async::Loop loop(&config);
    DispatchContext context = {};
    context.loop = &loop;
    fuchsia_hardware_usb_peripheral_Events_ops ops;
    ops.FunctionRegistered = DispatchStateChange;
    async_dispatcher_t* dispatcher = loop.dispatcher();
    fidl_bind(dispatcher, handles[0].get(), dispatch_wrapper, &context, &ops);
    loop.Run();

    ASSERT_TRUE(context.state_changed);
    ASSERT_EQ(ZX_OK, FidlCall(fuchsia_usb_virtualbus_BusConnect, virtual_bus_handle_.get()));
    fbl::unique_fd fd(openat(devmgr_.devfs_root().get(), "class/block", O_RDONLY));
    while (fdio_watch_directory(fd.get(), WaitForAnyFile, ZX_TIME_INFINITE, devpath) !=
            ZX_ERR_STOP) {
    }
    *devpath = fbl::String::Concat({fbl::String("class/block/"), *devpath});
}

// Initialize a Usb HID device. Asserts on failure.
void USBVirtualBus::InitUsbHid(fbl::String* devpath) {
    fuchsia_hardware_usb_peripheral_DeviceDescriptor device_desc = {};
    device_desc.bcdUSB = htole16(0x0200);
    device_desc.bMaxPacketSize0 = 64;
    device_desc.bcdDevice = htole16(0x0100);
    device_desc.bNumConfigurations = 1;
    ASSERT_EQ(ZX_OK, FidlCall(fuchsia_hardware_usb_peripheral_DeviceSetDeviceDescriptor,
                peripheral_.get(), &device_desc));

    fuchsia_hardware_usb_peripheral_FunctionDescriptor usb_hid_function_desc = {
        .interface_class = USB_CLASS_HID,
        .interface_subclass = 0,
        .interface_protocol = 0,
    };

    ASSERT_EQ(ZX_OK, FidlCall(fuchsia_hardware_usb_peripheral_DeviceAddFunction,
                peripheral_.get(), &usb_hid_function_desc));
    zx::channel handles[2];
    ASSERT_EQ(ZX_OK, zx::channel::create(0, handles, handles + 1));
    ASSERT_EQ(ZX_OK, fuchsia_hardware_usb_peripheral_DeviceSetStateChangeListener(
                peripheral_.get(), handles[1].get()));
    ASSERT_EQ(ZX_OK,
            FidlCall(fuchsia_hardware_usb_peripheral_DeviceBindFunctions, peripheral_.get()));
    async_loop_config_t config = {};
    async::Loop loop(&config);
    DispatchContext context = {};
    context.loop = &loop;
    fuchsia_hardware_usb_peripheral_Events_ops ops;
    ops.FunctionRegistered = DispatchStateChange;
    async_dispatcher_t* dispatcher = loop.dispatcher();
    fidl_bind(dispatcher, handles[0].get(), dispatch_wrapper, &context, &ops);
    loop.Run();

    ASSERT_TRUE(context.state_changed);
    ASSERT_EQ(ZX_OK, FidlCall(fuchsia_usb_virtualbus_BusConnect, virtual_bus_handle_.get()));
    fbl::unique_fd fd(openat(devmgr_.devfs_root().get(), "class/input", O_RDONLY));
    while (fdio_watch_directory(fd.get(), WaitForAnyFile, ZX_TIME_INFINITE, devpath) !=
            ZX_ERR_STOP) {
    }
    *devpath = fbl::String::Concat({fbl::String("class/input/"), *devpath});
}


void USBVirtualBus::GetHandles(zx::unowned_channel* peripheral, zx::unowned_channel* bus) {
    *peripheral = zx::unowned_channel(peripheral_);
    *bus = zx::unowned_channel(virtual_bus_handle_);
}

int USBVirtualBus::GetRootFd() { return devmgr_.devfs_root().get(); }

} // namespace usb_virtual_bus
