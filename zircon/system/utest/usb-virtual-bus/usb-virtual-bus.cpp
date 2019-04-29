// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blktest/blktest.h>
#include <block-client/client.h>
#include <ddk/platform-defs.h>
#include <dirent.h>
#include <endian.h>
#include <fbl/auto_call.h>
#include <fbl/function.h>
#include <fbl/string.h>
#include <fbl/unique_ptr.h>
#include <fcntl.h>
#include <fuchsia/hardware/usb/peripheral/block/c/fidl.h>
#include <fuchsia/hardware/usb/peripheral/c/fidl.h>
#include <fuchsia/usb/virtualbus/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/loop.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/spawn.h>
#include <lib/fdio/unsafe.h>
#include <lib/fdio/watcher.h>
#include <lib/fidl-async/bind.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/hw/usb.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zxtest/zxtest.h>

using driver_integration_test::IsolatedDevmgr;

namespace {

struct DispatchContext {
    bool state_changed;
    async::Loop* loop;
};

zx_status_t DispatchStateChange(void* ctx, fidl_txn_t* txn) {
    DispatchContext* context = reinterpret_cast<DispatchContext*>(ctx);
    context->state_changed = true;
    context->loop->Quit();
    return ZX_ERR_CANCELED;
}

zx_status_t dispatch_wrapper(void* ctx, fidl_txn_t* txn, fidl_msg_t* msg, const void* ops) {
    return fuchsia_hardware_usb_peripheral_Events_dispatch(
        ctx, txn, msg, reinterpret_cast<const fuchsia_hardware_usb_peripheral_Events_ops_t*>(ops));
}

template <typename F, typename... Args> zx_status_t FidlCall(const F& function, Args... args) {
    zx_status_t status;
    zx_status_t status1;
    status = function(args..., &status1);
    if (status) {
        return status;
    }
    return status1;
}

zx_status_t AllocateString(const zx::channel& handle, const char* string, uint8_t* out) {
    zx_status_t status1;
    zx_status_t status = fuchsia_hardware_usb_peripheral_DeviceAllocStringDesc(
        handle.get(), string, strlen(string), &status1, out);
    if (status) {
        return status;
    }
    return status1;
}

using Callback = fbl::Function<zx_status_t(int, const char*)>;
zx_status_t WatcherCallback(int dirfd, int event, const char* fn, void* cookie) {
    return (*reinterpret_cast<Callback*>(cookie))(event, fn);
}

zx_status_t WatchDirectory(int dirfd, Callback* callback) {
    return fdio_watch_directory(dirfd, WatcherCallback, ZX_TIME_INFINITE, callback);
}

zx_status_t WaitForAnyFile(int dirfd, int event, const char* name, void* cookie) {
    if (event != WATCH_EVENT_ADD_FILE) {
        return ZX_OK;
    }
    if (*name) {
        *reinterpret_cast<fbl::String*>(cookie) = fbl::String(name);
        return ZX_ERR_STOP;
    } else {
        return ZX_OK;
    }
}

zx_status_t WaitForFile(int dirfd, int event, const char* fn, void* name) {
    if (event != WATCH_EVENT_ADD_FILE) {
        return ZX_OK;
    }
    return strcmp(static_cast<const char*>(name), fn) ? ZX_OK : ZX_ERR_STOP;
}

class USBVirtualBus {
public:
    static zx_status_t create(USBVirtualBus* bus) { return ZX_OK; }

    // Initialize UMS. Asserts on failure.
    void InitUMS(fbl::String* devpath) {
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

    USBVirtualBus() {
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

    void GetHandles(zx::unowned_channel* peripheral, zx::unowned_channel* bus) {
        *peripheral = zx::unowned_channel(peripheral_);
        *bus = zx::unowned_channel(virtual_bus_handle_);
    }

    int GetRootFd() { return devmgr_.devfs_root().get(); }

private:
    IsolatedDevmgr::Args args_;
    IsolatedDevmgr devmgr_;
    zx::channel peripheral_;
    zx::channel virtual_bus_handle_;
    DISALLOW_COPY_ASSIGN_AND_MOVE(USBVirtualBus);
};

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
    USBVirtualBus bus_;
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
