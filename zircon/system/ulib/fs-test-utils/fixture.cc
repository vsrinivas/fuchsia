// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/unsafe.h>
#include <lib/memfs/memfs.h>
#include <lib/sync/completion.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <zircon/assert.h>
#include <zircon/device/vfs.h>
#include <zircon/errors.h>

#include <fbl/string_buffer.h>
#include <fbl/string_printf.h>
#include <fbl/unique_fd.h>
#include <fs-management/fvm.h>
#include <fs-test-utils/fixture.h>
#include <ramdevice-client/ramdisk.h>

namespace fs_test_utils {

namespace {

constexpr char kRamdiskCtlPath[] = "misc/ramctl";

constexpr char kDevPath[] = "/dev";
// Used as path for referencing devices bound to the isolated devmgr
// in the current test case.
constexpr char kIsolatedDevPath[] = "/isolated-dev";

// Mount point for local MemFs to be mounted.
constexpr char kMemFsPath[] = "/memfs";

// Name for MemFs serving thread.
constexpr char kMemFsThreadName[] = "TestServingMemFsName";

// Path where to mount the filesystem.
constexpr char kFsPath[] = "%s/fs-root";

// Partition name where the filesystem will be mounted when using fvm.
constexpr char kFsPartitionName[] = "fs-test-partition";

// FVM Driver lib
constexpr char kFvmDriverLibPath[] = "/boot/driver/fvm.so";

constexpr uint8_t kTestUniqueGUID[] = {0xFF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                       0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};

constexpr uint8_t kTestPartGUID[] = {0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
                                     0xFF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};

zx_status_t MountMemFs(async::Loop* loop, memfs_filesystem_t** memfs_out) {
  zx_status_t result = ZX_OK;
  result = loop->StartThread(kMemFsThreadName);
  if (result != ZX_OK) {
    LOG_ERROR(result, "Failed to start serving thread for MemFs.\n");
    return result;
  }

  return memfs_install_at(loop->dispatcher(), kMemFsPath, memfs_out);
}

zx_status_t UnmountMemFs(memfs_filesystem_t* memfs) {
  return memfs_uninstall_unsafe(memfs, kMemFsPath);
}

zx_status_t MakeRamdisk(int devfs_root, const FixtureOptions& options,
                        ramdisk_client_t** out_ramdisk) {
  ZX_DEBUG_ASSERT(options.use_ramdisk);
  if (options.use_ramdisk) {
    zx_status_t result = ramdisk_create_at(devfs_root, options.ramdisk_block_size,
                                           options.ramdisk_block_count, out_ramdisk);
    if (result != ZX_OK) {
      LOG_ERROR(result, "Failed to create ramdisk(block_size=%lu, ramdisk_block_count=%lu)\n",
                options.ramdisk_block_size, options.ramdisk_block_count);
      return result;
    }
  }

  return ZX_OK;
}

zx_status_t RemoveRamdisk(const FixtureOptions& options, ramdisk_client_t* ramdisk) {
  ZX_DEBUG_ASSERT(options.use_ramdisk);
  if (options.use_ramdisk && ramdisk != nullptr) {
    zx_status_t result = ramdisk_destroy(ramdisk);
    if (result != ZX_OK) {
      LOG_ERROR(result, "Failed to destroy ramdisk.\n");
    }
  }
  return ZX_OK;
}

zx_status_t MakeFvm(int devfs_root, const char* root_path, const fbl::String& block_device_path,
                    uint64_t fvm_slice_size,

                    fbl::String* partition_path, bool* fvm_mounted) {
  zx_status_t result = ZX_OK;
  fbl::unique_fd fd(open(block_device_path.c_str(), O_RDWR));
  if (!fd) {
    LOG_ERROR(ZX_ERR_IO, "%s.\nblock_device_path: %s\n", strerror(errno),
              block_device_path.c_str());
    return ZX_ERR_IO;
  }
  result = fvm_init(fd.get(), fvm_slice_size);
  if (result != ZX_OK) {
    LOG_ERROR(result, "Failed to format device with FVM.\nblock_device_path: %s\n",
              block_device_path.c_str());
    return result;
  }
  *fvm_mounted = true;
  fbl::String fvm_device_path = fbl::StringPrintf("%s/fvm", block_device_path.c_str());
  // Bind FVM Driver.
  fdio_t* io = fdio_unsafe_fd_to_io(fd.get());
  if (io == NULL) {
    return ZX_ERR_INTERNAL;
  }
  zx_status_t call_status = ZX_OK;
  auto resp = ::llcpp::fuchsia::device::Controller::Call::Bind(
      zx::unowned_channel(fdio_unsafe_borrow_channel(io)),
      ::fidl::StringView(kFvmDriverLibPath, strlen(kFvmDriverLibPath)));
  result = resp.status();
  if (resp->result.is_err()) {
    call_status = resp->result.err();
  }
  fdio_unsafe_release(io);
  if (result == ZX_OK) {
    result = call_status;
  }
  if (result != ZX_OK) {
    LOG_ERROR(result, "Failed to bind fvm driver to block device.\nblock_device:%s\n",
              block_device_path.c_str());
    return result;
  }

  result = wait_for_device(fvm_device_path.c_str(), zx::sec(3).get());
  if (result != ZX_OK) {
    LOG_ERROR(result, "FVM driver failed to start.\nfvm_device_path:%s\n", fvm_device_path.c_str());
    return result;
  }

  fbl::unique_fd fvm_fd(open(fvm_device_path.c_str(), O_RDWR));
  if (!fvm_fd) {
    LOG_ERROR(ZX_ERR_IO, "%s.\nfvm_device_path:%s\n", strerror(errno), fvm_device_path.c_str());
    return ZX_ERR_IO;
  }

  alloc_req_t request;
  memset(&request, 0, sizeof(request));
  request.slice_count = 1;
  strcpy(request.name, kFsPartitionName);
  memcpy(request.type, kTestPartGUID, sizeof(request.type));
  memcpy(request.guid, kTestUniqueGUID, sizeof(request.guid));

  fbl::unique_fd partition_fd(
      fvm_allocate_partition_with_devfs(devfs_root, fvm_fd.get(), &request));
  if (!partition_fd) {
    LOG_ERROR(ZX_ERR_IO, "Failed to allocate FVM partition. Error: %s \n", strerror(errno));
    return ZX_ERR_IO;
  }

  char buffer[kPathSize];
  partition_fd.reset(
      open_partition_with_devfs(devfs_root, kTestUniqueGUID, kTestPartGUID, 0, buffer));
  *partition_path = fbl::String::Concat({root_path, "/", buffer});
  if (!partition_fd) {
    LOG_ERROR(ZX_ERR_IO, "Could not locate FVM partition. %s\n", strerror(errno));
    return ZX_ERR_IO;
  }

  return ZX_OK;
}

}  // namespace

bool FixtureOptions::IsValid(fbl::String* err_description) const {
  ZX_DEBUG_ASSERT(err_description != nullptr);
  fbl::StringBuffer<400> buffer;
  err_description->clear();

  if (use_ramdisk) {
    if (!block_device_path.empty()) {
      buffer.Append("use_ramdisk and block_device_path are mutually exclusive.\n");
    }
    size_t max_size = zx_system_get_physmem();
    size_t requested_size = ramdisk_block_count * ramdisk_block_size;
    if (max_size < requested_size) {
      buffer.AppendPrintf("ramdisk size(%lu) cannot exceed available memory(%lu).\n",
                          requested_size, requested_size);
    }
    if (ramdisk_block_count == 0) {
      buffer.Append("ramdisk_block_count must be greater than 0.\n");
    }
    if (ramdisk_block_size == 0) {
      buffer.Append("ramdisk_block_size must be greater than 0.\n");
    }
  } else if (block_device_path.empty()) {
    buffer.Append("block_device_path or use_ramdisk must be set\n.");
  }

  if (use_fvm) {
    if (fvm_slice_size == 0 || fvm_slice_size % kFvmBlockSize != 0) {
      buffer.AppendPrintf("fvm_slize_size must be a multiple of %lu.\n", kFvmBlockSize);
    }
  }

  *err_description = buffer.ToString();

  return err_description->empty();
}

Fixture::Fixture(const FixtureOptions& options) : options_(options) {}

Fixture::~Fixture() {
  // Sanity check, teardown any resources if these two were not called yet.
  TearDown();
  TearDownTestCase();
}

zx_status_t Fixture::Mount() {
  fbl::unique_fd fd(open(GetFsBlockDevice().c_str(), O_RDWR));
  if (!fd) {
    LOG_ERROR(ZX_ERR_IO, "%s.\nblock_device_path:%s\n", strerror(errno),
              GetFsBlockDevice().c_str());
    return ZX_ERR_IO;
  }

  // Already mounted.
  if (fs_state_ == ResourceState::kAllocated) {
    return ZX_OK;
  }

  mount_options_t mount_options = default_mount_options;
  mount_options.create_mountpoint = true;
  mount_options.wait_until_ready = true;
  mount_options.register_fs = false;
  if (options_.use_pager) {
    mount_options.enable_pager = true;
  }
  if (options_.write_compression_algorithm) {
    mount_options.write_compression_algorithm = options_.write_compression_algorithm;
  }

  disk_format_t format = detect_disk_format(fd.get());
  zx_status_t result =
      mount(fd.release(), fs_path_.c_str(), format, &mount_options, launch_stdio_async);
  if (result != ZX_OK) {
    LOG_ERROR(result, "Failed to mount device at %s.\nblock_device_path:%s\n", fs_path_.c_str(),
              GetFsBlockDevice().c_str());
    return result;
  }
  fs_state_ = ResourceState::kAllocated;
  return ZX_OK;
}

zx_status_t Fixture::Fsck() const {
  const fbl::String& block_dev = GetFsBlockDevice().c_str();
  if (block_dev.empty()) {
    // the block device doesn't exist, in which case there's nothing to
    // check. since this is a test fixture, that's probably not what wanted,
    // so surface the error.
    LOG_ERROR(ZX_ERR_BAD_STATE, "Fsck called on an empty fixture\n");
    return ZX_ERR_BAD_STATE;
  }

  // set up the fsck options
  fsck_options_t fsck_options = default_fsck_options;
  fsck_options.never_modify = true;
  fsck_options.force = true;

  // run fsck
  zx_status_t result = fsck(block_dev.c_str(), options_.fs_type, &fsck_options, launch_stdio_sync);
  if (result != ZX_OK) {
    LOG_ERROR(result, "Fsck failed on device at block_device_path:%s\n", block_dev.c_str());
    return result;
  }

  return ZX_OK;
}

zx_status_t Fixture::Umount() {
  if (fs_state_ != ResourceState::kAllocated) {
    return ZX_OK;
  }
  if (!fs_path_.empty()) {
    zx_status_t result = umount(fs_path_.c_str());
    if (result != ZX_OK) {
      LOG_ERROR(result,
                "Failed to umount device from MemFs.\nblock_device_path:%s\nmount_path:%s\n",
                GetFsBlockDevice().c_str(), fs_path_.c_str());
      return result;
    }
    fs_state_ = ResourceState::kFreed;
  }
  return ZX_OK;
}

zx_status_t Fixture::Format() const {
  // Format device.
  mkfs_options_t mkfs_options = default_mkfs_options;
  fbl::String block_device_path = GetFsBlockDevice();
  zx_status_t result =
      mkfs(block_device_path.c_str(), options_.fs_type, launch_stdio_sync, &mkfs_options);
  if (result != ZX_OK) {
    LOG_ERROR(result, "Failed to format block device.\nblock_device_path:%s\n",
              block_device_path.c_str());
    return result;
  }

  // Verify format.
  fsck_options_t fsck_options = default_fsck_options;
  result = fsck(block_device_path.c_str(), options_.fs_type, &fsck_options, launch_stdio_sync);
  if (result != ZX_OK) {
    LOG_ERROR(result, "Block device format has errors.\nblock_device_path:%s\n",
              block_device_path.c_str());
    return result;
  }

  return ZX_OK;
}

zx_status_t Fixture::SetUpTestCase() {
  LOG_INFO("Using random seed: %u\n", options_.seed);
  zx_status_t result;
  seed_ = options_.seed;

  // Create the devmgr instance and bind it
  if (options_.isolated_devmgr) {
    devmgr_launcher::Args args = devmgr_integration_test::IsolatedDevmgr::DefaultArgs();
    args.disable_block_watcher = true;
    args.sys_device_driver = devmgr_integration_test::IsolatedDevmgr::kSysdevDriver;
    args.load_drivers.push_back(devmgr_integration_test::IsolatedDevmgr::kSysdevDriver);
    args.driver_search_paths.push_back("/boot/driver");

    result = devmgr_integration_test::IsolatedDevmgr::Create(std::move(args), &devmgr_);

    if (result != ZX_OK) {
      return result;
    }
    fdio_ns_t* ns;
    result = fdio_ns_get_installed(&ns);
    if (result != ZX_OK) {
      return result;
    }
    result = fdio_ns_bind_fd(ns, kIsolatedDevPath, devmgr_.devfs_root().get());
    if (result != ZX_OK) {
      return result;
    }
    // Wait for RamCtl to appear.
    result = wait_for_device_at(devmgr_.devfs_root().get(), kRamdiskCtlPath, zx::sec(5).get());

    if (result != ZX_OK) {
      return result;
    }
    devfs_root_.reset(open(kIsolatedDevPath, O_RDWR));
    root_path_ = kIsolatedDevPath;
  } else {
    devfs_root_.reset(open(kDevPath, O_RDWR));
    root_path_ = kDevPath;
  }

  if (options_.use_ramdisk) {
    result = MakeRamdisk(devfs_root_.get(), options_, &ramdisk_);
    if (result != ZX_OK) {
      return result;
    }
    block_device_path_ =
        fbl::StringPrintf("%s/%s", options_.isolated_devmgr ? kIsolatedDevPath : kDevPath,
                          ramdisk_get_path(ramdisk_));
    ramdisk_state_ = ResourceState::kAllocated;
  }

  if (!options_.block_device_path.empty()) {
    block_device_path_ = options_.block_device_path;
  }

  return ZX_OK;
}

zx_status_t Fixture::SetUp() {
  fvm_state_ = ResourceState::kUnallocated;
  fs_state_ = ResourceState::kUnallocated;
  if (options_.use_fvm) {
    bool allocated = false;
    zx_status_t result = MakeFvm(devfs_root_.get(), root_path_, block_device_path_,
                                 options_.fvm_slice_size, &partition_path_, &allocated);
    if (allocated) {
      fvm_state_ = ResourceState::kAllocated;
    }

    if (result != ZX_OK) {
      return result;
    }
  }

  fs_path_ = fbl::StringPrintf(kFsPath, kMemFsPath);
  zx_status_t result;
  if (options_.fs_format) {
    result = Format();
    if (result != ZX_OK) {
      return result;
    }
  }

  if (options_.fs_mount) {
    result = Mount();
    if (result != ZX_OK) {
      return result;
    }
    fs_state_ = ResourceState::kAllocated;
  }

  return ZX_OK;
}

zx_status_t Fixture::TearDown() {
  zx_status_t result;
  // Umount Fs from MemFs.
  if (fs_state_ == ResourceState::kAllocated) {
    result = Umount();
    if (result != ZX_OK) {
      return result;
    }
  }

  // If real device not in FVM, clean it.
  if (!block_device_path_.empty() && !options_.use_fvm && fs_state_ == ResourceState::kAllocated) {
    result = Format();
    if (result != ZX_OK) {
      return result;
    }
    fs_state_ = ResourceState::kFreed;
  }

  // If using FVM on top of device, just destroy the fvm, this only applies if
  // the fvm was created within this process.
  if (options_.use_fvm && fvm_state_ == ResourceState::kAllocated) {
    result = fvm_destroy(block_device_path_.c_str());
    if (result != ZX_OK) {
      LOG_ERROR(result, "Failed to destroy fvm in block_device.\nblock_device: %s\n",
                block_device_path_.cend());
      return result;
    }
    fs_state_ = ResourceState::kFreed;
    fvm_state_ = ResourceState::kFreed;
  }
  return ZX_OK;
}

zx_status_t Fixture::TearDownTestCase() {
  if (options_.isolated_devmgr) {
    zx_status_t result;
    fdio_ns_t* ns;
    result = fdio_ns_get_installed(&ns);
    if (result != ZX_OK) {
      return result;
    }
    result = fdio_ns_unbind(ns, kIsolatedDevPath);
    // Either successfuly unbind or we already unbound it.
    if (result != ZX_OK && result != ZX_ERR_NOT_FOUND) {
      return result;
    }
  }

  if (ramdisk_state_ == ResourceState::kAllocated) {
    zx_status_t ramdisk_result = RemoveRamdisk(options_, ramdisk_);
    ramdisk_ = nullptr;
    if (ramdisk_result != ZX_OK) {
      return ramdisk_result;
    }
  }
  ramdisk_state_ = ResourceState::kFreed;

  return ZX_OK;
}

int RunWithMemFs(const fbl::Function<int()>& main_fn) {
  memfs_filesystem_t* fs;
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  zx_status_t status;
  if ((status = MountMemFs(&loop, &fs)) != ZX_OK) {
    LOG_ERROR(status, "Failed to mount memfs\n");
    return -1;
  }
  int result = main_fn();
  loop.Shutdown();
  if ((status = UnmountMemFs(fs)) != ZX_OK) {
    LOG_ERROR(status, "Failed to unmount memfs\n");
    return -1;
  }
  return result;
}

}  // namespace fs_test_utils
