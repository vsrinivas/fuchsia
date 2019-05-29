// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-virtual-bus.h"
#include "helper.h"

#include <blktest/blktest.h>
#include <block-client/client.h>
#include <ddk/platform-defs.h>
#include <dirent.h>
#include <endian.h>
#include <fbl/auto_call.h>
#include <fbl/function.h>
#include <fbl/unique_ptr.h>
#include <fcntl.h>
#include <fuchsia/hardware/usb/peripheral/block/c/fidl.h>
#include <fuchsia/hardware/usb/peripheral/c/fidl.h>
#include <fuchsia/usb/virtualbus/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/loop.h>
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

namespace {

using driver_integration_test::IsolatedDevmgr;

class BlockDeviceController {
public:
    explicit BlockDeviceController(zx::unowned_channel peripheral, zx::unowned_channel bus,
                                   int root_fd)
        : peripheral_(peripheral), bus_(bus), root_fd_(root_fd) {}

    zx_status_t Disconnect() {
        zx_status_t status =
            FidlCall(fuchsia_hardware_usb_peripheral_DeviceClearFunctions, peripheral_->get());
        if (status != ZX_OK) {
            return status;
        }
        status = FidlCall(fuchsia_usb_virtualbus_BusDisconnect, bus_->get());
        if (status != ZX_OK) {
            return status;
        }

        return ZX_OK;
    }

    zx_status_t Connect() {
        fuchsia_hardware_usb_peripheral_FunctionDescriptor ums_function_desc = {
            .interface_class = USB_CLASS_MSC,
            .interface_subclass = USB_SUBCLASS_MSC_SCSI,
            .interface_protocol = USB_PROTOCOL_MSC_BULK_ONLY,
        };
        zx_status_t status = FidlCall(fuchsia_hardware_usb_peripheral_DeviceAddFunction,
                                      peripheral_->get(), &ums_function_desc);
        if (status != ZX_OK) {
            return status;
        }
        zx_handle_t handles[2];
        status = zx_channel_create(0, handles, handles + 1);
        if (status != ZX_OK) {
            return status;
        }
        status = fuchsia_hardware_usb_peripheral_DeviceSetStateChangeListener(peripheral_->get(),
                                                                              handles[1]);
        if (status != ZX_OK) {
            return status;
        }
        async_loop_config_t config = {};
        async::Loop loop(&config);
        DispatchContext context = {};
        context.loop = &loop;
        fuchsia_hardware_usb_peripheral_Events_ops ops;
        ops.FunctionRegistered = DispatchStateChange;
        async_dispatcher_t* dispatcher = loop.dispatcher();
        fidl_bind(dispatcher, handles[0], dispatch_wrapper, &context, &ops);
        status = loop.StartThread("async-thread");
        if (status != ZX_OK) {
            return status;
        }
        status = FidlCall(fuchsia_hardware_usb_peripheral_DeviceBindFunctions, peripheral_->get());
        if (status != ZX_OK) {
            return status;
        }
        loop.JoinThreads();
        fbl::String devpath;
        while (fdio_watch_directory(openat(root_fd_, "class/usb-cache-test", O_RDONLY),
                                    WaitForAnyFile, ZX_TIME_INFINITE, &devpath) != ZX_ERR_STOP)
            ;
        devpath = fbl::String::Concat({fbl::String("class/usb-cache-test/"), devpath});
        fbl::unique_fd fd(openat(root_fd_, devpath.c_str(), O_RDWR));
        status = fdio_get_service_handle(fd.release(), cachecontrol_.reset_and_get_address());
        if (status != ZX_OK) {
            return status;
        }
        if (!context.state_changed) {
            return ZX_ERR_INTERNAL;
        }
        if (status != ZX_OK) {
            return status;
        }
        status = FidlCall(fuchsia_usb_virtualbus_BusConnect, bus_->get());
        return ZX_OK;
    }

    zx_status_t EnableWritebackCache() {
        return FidlCall(fuchsia_hardware_usb_peripheral_block_DeviceEnableWritebackCache,
                        cachecontrol_.get());
    }

    zx_status_t DisableWritebackCache() {
        return FidlCall(fuchsia_hardware_usb_peripheral_block_DeviceDisableWritebackCache,
                        cachecontrol_.get());
    }

    zx_status_t SetWritebackCacheReported(bool report) {
        return FidlCall(fuchsia_hardware_usb_peripheral_block_DeviceSetWritebackCacheReported,
                        cachecontrol_.get(), report);
    }

private:
    zx::unowned_channel peripheral_;
    zx::unowned_channel bus_;
    zx::channel cachecontrol_;
    int root_fd_;
};

class UmsTest : public zxtest::Test {
public:
    void SetUp() override;
    void TearDown() override;

protected:
    usb_virtual_bus::USBVirtualBus bus_;
    fbl::String devpath_;
    zx::unowned_channel peripheral_;
    zx::unowned_channel virtual_bus_handle_;
    fbl::String GetTestdevPath() {
        // Open the block device
        // Special case for bad block mode. Need to enumerate the singleton block device.
        // NOTE: This MUST be a tight loop with NO sleeps in order to reproduce
        // the block-watcher deadlock. Changing the timing even slightly
        // makes this test invalid.
        while (true) {
            fbl::unique_fd fd(openat(bus_.GetRootFd(), "class/block", O_RDONLY));
            DIR* dir_handle = fdopendir(fd.get());
            fbl::AutoCall release_dir([=]() { closedir(dir_handle); });
            for (dirent* ent = readdir(dir_handle); ent; ent = readdir(dir_handle)) {
                if (strcmp(ent->d_name, ".") && strcmp(ent->d_name, "..")) {
                    last_known_devpath_ = fbl::String::Concat(
                        {fbl::String("class/block/"), fbl::String(ent->d_name)});
                    return last_known_devpath_;
                }
            }
        }
    }

    // Waits for the block device to be removed
    // TODO (ZX-3385, ZX-3586) -- Use something better
    // than a busy loop.
    void WaitForRemove() {
        struct stat dirinfo;
        // NOTE: This MUST be a tight loop with NO sleeps in order to reproduce
        // the block-watcher deadlock. Changing the timing even slightly
        // makes this test invalid.
        while (!stat(last_known_devpath_.c_str(), &dirinfo))
            ;
    }

private:
    fbl::String last_known_devpath_;
};

void UmsTest::SetUp() {
    ASSERT_NO_FATAL_FAILURES(bus_.InitUMS(&devpath_));
    bus_.GetHandles(&peripheral_, &virtual_bus_handle_);
}

void UmsTest::TearDown() {
    ASSERT_EQ(ZX_OK,
              FidlCall(fuchsia_hardware_usb_peripheral_DeviceClearFunctions, peripheral_->get()));
    ASSERT_EQ(ZX_OK, FidlCall(fuchsia_usb_virtualbus_BusDisable, virtual_bus_handle_->get()));
}

TEST_F(UmsTest, ReconnectTest) {
    // Disconnect and re-connect the block device 50 times as a sanity check
    // for race conditions and deadlocks.
    // If the test freezes; or something crashes at this point, it is likely
    // a regression in a driver (not a test flake).
    BlockDeviceController controller(zx::unowned_channel(peripheral_),
                                     zx::unowned_channel(virtual_bus_handle_), bus_.GetRootFd());
    for (size_t i = 0; i < 50; i++) {
        ASSERT_OK(controller.Disconnect());
        WaitForRemove();
        ASSERT_OK(controller.Connect());
        GetTestdevPath();
    }
    ASSERT_OK(controller.Disconnect());
}

TEST_F(UmsTest, CachedWriteWithNoFlushShouldBeDiscarded) {
    // Enable writeback caching on the block device
    BlockDeviceController controller(zx::unowned_channel(peripheral_),
                                     zx::unowned_channel(virtual_bus_handle_), bus_.GetRootFd());
    ASSERT_OK(controller.Disconnect());
    ASSERT_OK(controller.Connect());
    ASSERT_OK(controller.SetWritebackCacheReported(true));
    ASSERT_OK(controller.EnableWritebackCache());
    fbl::unique_fd fd(openat(bus_.GetRootFd(), GetTestdevPath().c_str(), O_RDWR));
    block_info_t info;
    __UNUSED ssize_t rc = ioctl_block_get_info(fd.get(), &info);
    uint32_t blk_size = info.block_size;
    fbl::unique_ptr<uint8_t[]> write_buffer(new uint8_t[blk_size]);
    fbl::unique_ptr<uint8_t[]> read_buffer(new uint8_t[blk_size]);
    ASSERT_EQ(blk_size, static_cast<uint64_t>(read(fd.get(), read_buffer.get(), blk_size)));
    close(fd.release());
    fd = fbl::unique_fd(openat(bus_.GetRootFd(), GetTestdevPath().c_str(), O_RDWR));
    // Create a pattern to write to the block device
    for (size_t i = 0; i < blk_size; i++) {
        write_buffer.get()[i] = static_cast<unsigned char>(i);
    }
    // Write the data to the block device
    ASSERT_EQ(blk_size, static_cast<uint64_t>(write(fd.get(), write_buffer.get(), blk_size)));
    ASSERT_EQ(-1, fsync(fd.get()));
    close(fd.release());
    // Disconnect the block device without flushing the cache.
    // This will cause the data that was written to be discarded.
    ASSERT_OK(controller.Disconnect());
    ASSERT_OK(controller.Connect());
    fd = fbl::unique_fd(openat(bus_.GetRootFd(), GetTestdevPath().c_str(), O_RDWR));
    ASSERT_EQ(blk_size, static_cast<uint64_t>(read(fd.get(), write_buffer.get(), blk_size)));
    ASSERT_NE(0, memcmp(read_buffer.get(), write_buffer.get(), blk_size));
}

TEST_F(UmsTest, UncachedWriteShouldBePersistedToBlockDevice) {
    BlockDeviceController controller(zx::unowned_channel(peripheral_),
                                     zx::unowned_channel(virtual_bus_handle_), bus_.GetRootFd());
    // Disable writeback caching on the device
    ASSERT_OK(controller.Disconnect());
    ASSERT_OK(controller.Connect());
    ASSERT_OK(controller.SetWritebackCacheReported(false));
    ASSERT_OK(controller.DisableWritebackCache());
    fbl::unique_fd fd(openat(bus_.GetRootFd(), GetTestdevPath().c_str(), O_RDWR));
    block_info_t info;
    __UNUSED ssize_t rc = ioctl_block_get_info(fd.get(), &info);
    uint32_t blk_size = info.block_size;
    // Allocate our buffer
    fbl::unique_ptr<uint8_t[]> write_buffer(new uint8_t[blk_size]);
    // Generate and write a pattern to the block device
    for (size_t i = 0; i < blk_size; i++) {
        write_buffer.get()[i] = static_cast<unsigned char>(i);
    }
    ASSERT_EQ(blk_size, static_cast<uint64_t>(write(fd.get(), write_buffer.get(), blk_size)));
    memset(write_buffer.get(), 0, blk_size);
    close(fd.release());
    // Disconnect and re-connect the block device
    ASSERT_OK(controller.Disconnect());
    ASSERT_OK(controller.Connect());
    fd = fbl::unique_fd(openat(bus_.GetRootFd(), GetTestdevPath().c_str(), O_RDWR));
    // Read back the pattern, which should match what was written
    // since writeback caching was disabled.
    ASSERT_EQ(blk_size, static_cast<uint64_t>(read(fd.get(), write_buffer.get(), blk_size)));
    for (size_t i = 0; i < blk_size; i++) {
        ASSERT_EQ(write_buffer.get()[i], static_cast<unsigned char>(i));
    }
}

TEST_F(UmsTest, BlkdevTest) {
    char errmsg[1024];
    fdio_spawn_action_t actions[1];
    actions[0] = {};
    actions[0].action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY;
    zx_handle_t fd_channel;
    ASSERT_OK(fdio_fd_clone(bus_.GetRootFd(), &fd_channel));
    actions[0].ns.handle = fd_channel;
    actions[0].ns.prefix = "/dev2";
    fbl::String path = fbl::String::Concat({fbl::String("/dev2/"), GetTestdevPath()});
    const char* argv[] = {"/boot/bin/blktest", "-d", path.c_str(), nullptr};
    zx_handle_t process;
    ASSERT_OK(fdio_spawn_etc(zx_job_default(), FDIO_SPAWN_CLONE_ALL, "/boot/bin/blktest", argv,
                             nullptr, 1, actions, &process, errmsg));
    uint32_t observed;
    zx_object_wait_one(process, ZX_PROCESS_TERMINATED, ZX_TIME_INFINITE, &observed);
    zx_info_process_t proc_info;
    EXPECT_OK(zx_object_get_info(process, ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), nullptr,
                                 nullptr));
    EXPECT_EQ(proc_info.return_code, 0);
}

} // namespace
