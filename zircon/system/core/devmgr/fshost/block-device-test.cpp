// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>

#include <lib/devmgr-integration-test/fixture.h>
#include <lib/fdio/namespace.h>
#include <ramdevice-client/ramdisk.h>
#include <zircon/assert.h>
#include <zircon/hw/gpt.h>
#include <zxtest/zxtest.h>

#include "block-device.h"
#include "filesystem-mounter.h"
#include "fs-manager.h"

namespace devmgr {
namespace {

using devmgr_integration_test::IsolatedDevmgr;

class BlockDeviceHarness : public zxtest::Test {
public:
    void SetUp() override {
        zx::event event;
        ASSERT_OK(zx::event::create(0, &event));
        ASSERT_OK(event.duplicate(ZX_RIGHT_SAME_RIGHTS, &event_));

        // Initialize FilesystemMounter.
        ASSERT_OK(FsManager::Create(std::move(event), &manager_));

        // Fshost really likes mounting filesystems at "/fs".
        // Let's make that available in our namespace.
        zx::channel client, server;
        ASSERT_OK(zx::channel::create(0, &client, &server));
        ASSERT_OK(manager_->ServeRoot(std::move(server)));
        fdio_ns_t* ns;
        ASSERT_OK(fdio_ns_get_installed(&ns));
        ASSERT_OK(fdio_ns_bind(ns, "/fs", client.release()));
        manager_->WatchExit();

        devmgr_launcher::Args args;
        args.disable_block_watcher = true;
        args.sys_device_driver = devmgr_integration_test::IsolatedDevmgr::kSysdevDriver;
        args.load_drivers.push_back(devmgr_integration_test::IsolatedDevmgr::kSysdevDriver);
        args.driver_search_paths.push_back("/boot/driver");
        ASSERT_OK(IsolatedDevmgr::Create(std::move(args), &devmgr_));
        fbl::unique_fd ctl;
        ASSERT_EQ(devmgr_integration_test::RecursiveWaitForFile(devmgr_.devfs_root(), "misc/ramctl",
                                                                zx::deadline_after(zx::sec(5)),
                                                                &ctl),
                  ZX_OK);
    }

    void TearDown() override {
        fdio_ns_t* ns;
        ASSERT_OK(fdio_ns_get_installed(&ns));
        fdio_ns_unbind(ns, "/fs");
    }

    std::unique_ptr<FsManager> TakeManager() {
        return std::move(manager_);
    }

    fbl::unique_fd devfs_root() {
        return devmgr_.devfs_root().duplicate();
    }

private:
    zx::event event_;
    std::unique_ptr<FsManager> manager_;
    IsolatedDevmgr devmgr_;
};

TEST_F(BlockDeviceHarness, TestBadHandleDevice) {
    std::unique_ptr<FsManager> manager = TakeManager();
    bool netboot = false;
    bool check_filesystems = false;
    FilesystemMounter mounter(std::move(manager), netboot, check_filesystems);
    fbl::unique_fd fd;
    BlockDevice device(&mounter, std::move(fd));
    EXPECT_EQ(device.Netbooting(), netboot);
    EXPECT_EQ(device.GetFormat(), DISK_FORMAT_UNKNOWN);
    fuchsia_hardware_block_BlockInfo info;
    EXPECT_EQ(device.GetInfo(&info), ZX_ERR_BAD_HANDLE);
    fuchsia_hardware_block_partition_GUID guid;
    EXPECT_EQ(device.GetTypeGUID(&guid), ZX_ERR_BAD_HANDLE);
    EXPECT_EQ(device.AttachDriver("/foobar"), ZX_ERR_BAD_HANDLE);

    // Returns ZX_OK because zxcrypt currently passes the empty fd to a background
    // thread without observing the results.
    EXPECT_OK(device.UnsealZxcrypt());

    // Returns ZX_OK because filesystem checks are disabled.
    EXPECT_OK(device.CheckFilesystem());

    EXPECT_EQ(device.FormatFilesystem(), ZX_ERR_BAD_HANDLE);
    EXPECT_EQ(device.MountFilesystem(), ZX_ERR_BAD_HANDLE);
}

TEST_F(BlockDeviceHarness, TestEmptyDevice) {
    std::unique_ptr<FsManager> manager = TakeManager();
    bool netboot = false;
    bool check_filesystems = false;
    FilesystemMounter mounter(std::move(manager), netboot, check_filesystems);

    // Initialize Ramdisk.
    constexpr uint64_t kBlockSize = 512;
    constexpr uint64_t kBlockCount = 1 << 20;
    ramdisk_client_t* ramdisk;
    ASSERT_OK(ramdisk_create_at(devfs_root().get(), kBlockSize, kBlockCount, &ramdisk));
    fbl::unique_fd fd;
    ASSERT_EQ(devmgr_integration_test::RecursiveWaitForFile(devfs_root(),
                                                            ramdisk_get_path(ramdisk),
                                                            zx::deadline_after(zx::sec(5)), &fd),
              ZX_OK);
    ASSERT_TRUE(fd);

    BlockDevice device(&mounter, std::move(fd));
    EXPECT_EQ(device.Netbooting(), netboot);
    EXPECT_EQ(device.GetFormat(), DISK_FORMAT_UNKNOWN);
    fuchsia_hardware_block_BlockInfo info;
    EXPECT_OK(device.GetInfo(&info));
    EXPECT_EQ(info.block_count, kBlockCount);
    EXPECT_EQ(info.block_size, kBlockSize);

    // Black-box: Since we're caching info, double check that re-calling GetInfo
    // works correctly.
    memset(&info, 0, sizeof(info));
    EXPECT_OK(device.GetInfo(&info));
    EXPECT_EQ(info.block_count, kBlockCount);
    EXPECT_EQ(info.block_size, kBlockSize);

    fuchsia_hardware_block_partition_GUID guid;
    EXPECT_OK(device.GetTypeGUID(&guid));

    EXPECT_EQ(device.FormatFilesystem(), ZX_ERR_NOT_SUPPORTED);
    EXPECT_EQ(device.MountFilesystem(), ZX_ERR_NOT_SUPPORTED);
    ASSERT_OK(ramdisk_destroy(ramdisk));
}

TEST_F(BlockDeviceHarness, TestMinfsBadGUID) {
    std::unique_ptr<FsManager> manager = TakeManager();
    bool netboot = false;
    bool check_filesystems = false;
    FilesystemMounter mounter(std::move(manager), netboot, check_filesystems);

    // Initialize Ramdisk with an empty GUID.
    constexpr uint64_t kBlockSize = 512;
    constexpr uint64_t kBlockCount = 1 << 20;
    ramdisk_client_t* ramdisk;
    ASSERT_OK(ramdisk_create_at(devfs_root().get(), kBlockSize, kBlockCount, &ramdisk));
    fbl::unique_fd fd;
    ASSERT_EQ(devmgr_integration_test::RecursiveWaitForFile(devfs_root(),
                                                            ramdisk_get_path(ramdisk),
                                                            zx::deadline_after(zx::sec(5)), &fd),
              ZX_OK);
    ASSERT_TRUE(fd);

    // We started with an empty block device, but let's lie and say it
    // should have been a minfs device.
    BlockDevice device(&mounter, std::move(fd));
    device.SetFormat(DISK_FORMAT_MINFS);
    EXPECT_EQ(device.GetFormat(), DISK_FORMAT_MINFS);
    EXPECT_OK(device.FormatFilesystem());

    // Unlike earlier, where we received "ERR_NOT_SUPPORTED", we get "ERR_WRONG_TYPE"
    // because the ramdisk doesn't have a data GUID.
    EXPECT_EQ(device.MountFilesystem(), ZX_ERR_WRONG_TYPE);

    ASSERT_OK(ramdisk_destroy(ramdisk));
}

TEST_F(BlockDeviceHarness, TestMinfsGoodGUID) {
    std::unique_ptr<FsManager> manager = TakeManager();

    bool netboot = false;
    bool check_filesystems = false;
    FilesystemMounter mounter(std::move(manager), netboot, check_filesystems);

    // Initialize Ramdisk with a data GUID.
    constexpr uint64_t kBlockSize = 512;
    constexpr uint64_t kBlockCount = 1 << 20;
    ramdisk_client_t* ramdisk;
    const uint8_t data_guid[GPT_GUID_LEN] = GUID_DATA_VALUE;
    ASSERT_OK(ramdisk_create_at_with_guid(devfs_root().get(), kBlockSize, kBlockCount,
                                          data_guid, sizeof(data_guid), &ramdisk));
    fbl::unique_fd fd;
    ASSERT_EQ(devmgr_integration_test::RecursiveWaitForFile(devfs_root(),
                                                            ramdisk_get_path(ramdisk),
                                                            zx::deadline_after(zx::sec(5)), &fd),
              ZX_OK);
    ASSERT_TRUE(fd);

    BlockDevice device(&mounter, std::move(fd));
    device.SetFormat(DISK_FORMAT_MINFS);
    EXPECT_EQ(device.GetFormat(), DISK_FORMAT_MINFS);
    EXPECT_OK(device.FormatFilesystem());

    EXPECT_OK(device.MountFilesystem());
    EXPECT_EQ(device.MountFilesystem(), ZX_ERR_ALREADY_BOUND);

    ASSERT_OK(ramdisk_destroy(ramdisk));
}

TEST_F(BlockDeviceHarness, TestMinfsReformat) {
    std::unique_ptr<FsManager> manager = TakeManager();

    bool netboot = false;
    bool check_filesystems = true;
    FilesystemMounter mounter(std::move(manager), netboot, check_filesystems);

    // Initialize Ramdisk with a data GUID.
    constexpr uint64_t kBlockSize = 512;
    constexpr uint64_t kBlockCount = 1 << 20;
    ramdisk_client_t* ramdisk;
    const uint8_t data_guid[GPT_GUID_LEN] = GUID_DATA_VALUE;
    ASSERT_OK(ramdisk_create_at_with_guid(devfs_root().get(), kBlockSize, kBlockCount,
                                          data_guid,
                                          sizeof(data_guid), &ramdisk));
    fbl::unique_fd fd;
    ASSERT_EQ(devmgr_integration_test::RecursiveWaitForFile(devfs_root(),
                                                            ramdisk_get_path(ramdisk),
                                                            zx::deadline_after(zx::sec(5)), &fd),
              ZX_OK);

    ASSERT_TRUE(fd);

    BlockDevice device(&mounter, std::move(fd));
    device.SetFormat(DISK_FORMAT_MINFS);
    EXPECT_EQ(device.GetFormat(), DISK_FORMAT_MINFS);

    // Before formatting the device, this isn't a valid minfs partition.
    EXPECT_NOT_OK(device.CheckFilesystem());
    EXPECT_NOT_OK(device.MountFilesystem());

    // After formatting the device, it is a valid partition. We can check the device,
    // and also mount it.
    EXPECT_OK(device.FormatFilesystem());
    EXPECT_OK(device.CheckFilesystem());
    EXPECT_OK(device.MountFilesystem());

    ASSERT_OK(ramdisk_destroy(ramdisk));
}

TEST_F(BlockDeviceHarness, TestBlobfs) {
    std::unique_ptr<FsManager> manager = TakeManager();

    bool netboot = false;
    bool check_filesystems = true;
    FilesystemMounter mounter(std::move(manager), netboot, check_filesystems);

    // Initialize Ramdisk with a data GUID.
    constexpr uint64_t kBlockSize = 512;
    constexpr uint64_t kBlockCount = 1 << 20;
    ramdisk_client_t* ramdisk;
    const uint8_t data_guid[GPT_GUID_LEN] = GUID_BLOB_VALUE;
    ASSERT_OK(ramdisk_create_at_with_guid(devfs_root().get(), kBlockSize, kBlockCount, data_guid,
                                          sizeof(data_guid), &ramdisk));
    fbl::unique_fd fd;
    ASSERT_EQ(devmgr_integration_test::RecursiveWaitForFile(devfs_root(),
                                                            ramdisk_get_path(ramdisk),
                                                            zx::deadline_after(zx::sec(5)), &fd),
              ZX_OK);
    ASSERT_TRUE(fd);

    BlockDevice device(&mounter, std::move(fd));
    device.SetFormat(DISK_FORMAT_BLOBFS);
    EXPECT_EQ(device.GetFormat(), DISK_FORMAT_BLOBFS);

    // Before formatting the device, this isn't a valid blobfs partition.
    // However, as implemented, we always validate the consistency of the filesystem.
    EXPECT_OK(device.CheckFilesystem());
    EXPECT_NOT_OK(device.MountFilesystem());

    // Additionally, blobfs does not yet support reformatting within fshost.
    EXPECT_NOT_OK(device.FormatFilesystem());
    EXPECT_OK(device.CheckFilesystem());
    EXPECT_NOT_OK(device.MountFilesystem());

    ASSERT_OK(ramdisk_destroy(ramdisk));
}

// TODO: Add tests for Zxcrypt binding.

} // namespace
} // namespace devmgr
