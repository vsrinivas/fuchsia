// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fs/test_support/environment.h"

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <getopt.h>
#include <lib/devmgr-launcher/launch.h>
#include <lib/fdio/namespace.h>
#include <lib/fzl/fdio.h>
#include <limits.h>
#include <sys/stat.h>
#include <zircon/status.h>

#include <fbl/unique_fd.h>
#include <fs/test_support/test_support.h>
#include <zxtest/zxtest.h>

namespace {

constexpr char kUsageMessage[] = R"""(
Tests can be run either against a real block device or using a ram-disk (default
behavior).

Options:
--device path_to_device (-d): Performs tests on top of a specific block device
--no-journal: Don't use journal
--pager: Use pager (if supported by the filesystem)
--help (-h): Displays full help

)""";

constexpr char kTestDevRoot[] = "/fake/dev";

bool GetOptions(int argc, char** argv, fs::Environment::TestConfig* config) {
  while (true) {
    struct option options[] = {
        {"device", required_argument, nullptr, 'd'},
        {"no-journal", no_argument, nullptr, 'j'},
        {"pager", no_argument, nullptr, 'p'},
        {"help", no_argument, nullptr, 'h'},
        {"gtest_filter", optional_argument, nullptr, 'f'},
        {"gtest_list_tests", optional_argument, nullptr, 'l'},
        {"gtest_shuffle", optional_argument, nullptr, 's'},
        {"gtest_repeat", required_argument, nullptr, 'i'},
        {"gtest_random_seed", required_argument, nullptr, 'r'},
        {"gtest_break_on_failure", optional_argument, nullptr, 'b'},
        {nullptr, 0, nullptr, 0},
    };
    int opt_index;
    int c = getopt_long(argc, argv, "d:jphf::l::s::i:r:b::", options, &opt_index);
    if (c < 0) {
      break;
    }
    switch (c) {
      case 'd':
        config->physical_device_path = optarg;
        break;
      case 'j':
        config->use_journal = false;
        break;
      case 'p':
        config->use_pager = true;
        break;
      case 'h':
        config->show_help = true;
        return true;
      case '?':
        return false;
    }
  }
  return argc == optind;
}

bool GetBlockInfo(zx_handle_t channel, fuchsia_hardware_block_BlockInfo* block_info) {
  zx_status_t status;
  zx_status_t io_status = fuchsia_hardware_block_BlockGetInfo(channel, &status, block_info);
  if (io_status != ZX_OK) {
    status = io_status;
  }

  if (status != ZX_OK) {
    printf("Could not query block device info: %s\n", zx_status_get_string(status));
    return false;
  }
  return true;
}

}  // namespace

namespace fs {

RamDisk::RamDisk(const fbl::unique_fd& devfs_root, uint32_t page_size, uint32_t num_pages)
    : page_size_(page_size), num_pages_(num_pages), path_(kTestDevRoot) {
  ASSERT_OK(ramdisk_create_at(devfs_root.get(), page_size_, num_pages_, &ramdisk_));
  path_.append("/");
  path_.append(ramdisk_get_path(ramdisk_));
}

RamDisk::~RamDisk() {
  if (ramdisk_) {
    ASSERT_OK(ramdisk_destroy(ramdisk_));
  }
}

zx_status_t RamDisk::SleepAfter(uint32_t block_count) const {
  return ramdisk_sleep_after(ramdisk_, block_count);
}

zx_status_t RamDisk::WakeUp() const { return ramdisk_wake(ramdisk_); }

zx_status_t RamDisk::GetBlockCounts(ramdisk_block_write_counts_t* counts) const {
  return ramdisk_get_block_counts(ramdisk_, counts);
}

bool Environment::TestConfig::GetOptions(int argc, char** argv) {
  return ::GetOptions(argc, argv, this);
}

const char* Environment::TestConfig::HelpMessage() const { return kUsageMessage; }

void Environment::SetUp() {
  ASSERT_NO_FAILURES(CreateDevmgr());

  if (config_.physical_device_path) {
    ASSERT_TRUE(OpenDevice(config_.physical_device_path));
  } else {
    block_count_ = config_.ramdisk_block_count;
    ramdisk_ = std::make_unique<RamDisk>(devfs_root(), block_size_, block_count_);
    path_.assign(ramdisk_->path());
  }
  ASSERT_TRUE(mkdir(config_.mount_path, 0755) == 0 || errno == EEXIST);
}

void Environment::TearDown() {
  ramdisk_.reset();
  fdio_ns_t* name_space;
  ASSERT_OK(fdio_ns_get_installed(&name_space));
  ASSERT_OK(fdio_ns_unbind(name_space, kTestDevRoot));
}

const char* Environment::GetRelativeDevicePath() const {
  if (!ramdisk_) {
    return device_path();
  }

  return device_path() + sizeof("/fake") - 1;  // -1 to count the number of characters.
}

bool Environment::OpenDevice(const char* path) {
  fbl::unique_fd fd(open(path, O_RDWR));
  if (!fd) {
    printf("Could not open block device\n");
    return false;
  }
  fzl::FdioCaller caller(std::move(fd));

  path_.assign(GetTopologicalPath(caller.borrow_channel()));
  if (path_.empty()) {
    return false;
  }

  // If we previously tried running tests on this disk, it may have created an
  // FVM and failed. Clean up from previous state before re-running.
  fvm_destroy(device_path());

  fuchsia_hardware_block_BlockInfo block_info;
  if (!GetBlockInfo(caller.borrow_channel(), &block_info)) {
    return false;
  }

  block_size_ = block_info.block_size;
  block_count_ = block_info.block_count;

  // Minimum size required by CreateUmountRemountLargeMultithreaded test.
  const uint64_t kMinDisksize = 5 * (1 << 20);  // 5 MB.

  if (disk_size() < kMinDisksize) {
    printf("Insufficient disk space for tests");
    return false;
  }

  return true;
}

void Environment::CreateDevmgr() {
  devmgr_launcher::Args args = devmgr_integration_test::IsolatedDevmgr::DefaultArgs();
  args.disable_block_watcher = true;
  args.disable_netsvc = true;
  args.driver_search_paths.push_back("/boot/driver");
  ASSERT_OK(devmgr_integration_test::IsolatedDevmgr::Create(std::move(args), &devmgr_));
  ASSERT_OK(wait_for_device_at(devfs_root().get(), "misc/ramctl", ZX_SEC(5)));

  fdio_ns_t* name_space;
  ASSERT_OK(fdio_ns_get_installed(&name_space));
  ASSERT_OK(fdio_ns_bind_fd(name_space, kTestDevRoot, devfs_root().get()));
}

}  // namespace fs
