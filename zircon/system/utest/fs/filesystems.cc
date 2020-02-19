// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "filesystems.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/namespace.h>
#include <lib/zx/channel.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/device/block.h>

#include <fs-management/fvm.h>
#include <fs-management/mount.h>
#include <fvm/format.h>
#include <ramdevice-client/ramdisk.h>

const char* kTmpfsPath = "/fs-test-tmp";
const char* kDevPath = "/dev";

bool use_real_disk = false;
fuchsia_hardware_block_BlockInfo test_disk_info;
char test_disk_path[PATH_MAX];
devmgr_integration_test::IsolatedDevmgr isolated_devmgr;
ramdisk_client_t* test_ramdisk = nullptr;
fs_info_t* test_info;

static char fvm_disk_path[PATH_MAX];

constexpr const char minfs_name[] = "minfs";
constexpr const char memfs_name[] = "memfs";

const fsck_options_t test_fsck_options = {
    .verbose = false,
    .never_modify = true,
    .always_modify = false,
    .force = true,
    .apply_journal = false,
};

#define FVM_DRIVER_LIB "/boot/driver/fvm.so"
#define STRLEN(s) (sizeof(s) / sizeof((s)[0]))

const test_disk_t default_test_disk = {
    .block_count = TEST_BLOCK_COUNT_DEFAULT,
    .block_size = TEST_BLOCK_SIZE_DEFAULT,
    .slice_size = TEST_FVM_SLICE_SIZE_DEFAULT,
};

constexpr uint8_t kTestUniqueGUID[] = {0xFF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                       0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};

constexpr uint8_t kTestPartGUID[] = {0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
                                     0xFF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};

void setup_fs_test(test_disk_t disk, fs_test_type_t test_class) {
  int r = mkdir(kMountPath, 0755);
  if ((r < 0) && errno != EEXIST) {
    fprintf(stderr, "Could not create mount point for test filesystem\n");
    exit(-1);
  }

  if (!use_real_disk) {
    // First, initialize a new isolated devmgr for the test environment.
    devmgr_launcher::Args args = devmgr_integration_test::IsolatedDevmgr::DefaultArgs();
    args.disable_block_watcher = true;
    args.disable_netsvc = true;
    args.driver_search_paths.push_back("/boot/driver");
    zx_status_t status =
        devmgr_integration_test::IsolatedDevmgr::Create(std::move(args), &isolated_devmgr);
    if (status != ZX_OK) {
      fprintf(stderr, "[FAILED]: Could not created isolated devmgr\n");
      exit(-1);
    }
    status = wait_for_device_at(isolated_devmgr.devfs_root().get(), "misc/ramctl",
                                zx::duration::infinite().get());
    if (status != ZX_OK) {
      fprintf(stderr, "[FAILED]: Could wait for ramctl\n");
      exit(-1);
    }

    // Modify the process namespace to refer to this isolated devmgr.
    fdio_ns_t* name_space;
    status = fdio_ns_get_installed(&name_space);
    if (status != ZX_OK) {
      fprintf(stderr, "[FAILED]: Could not acquire namespace\n");
      exit(-1);
    }
    // Intentionally avoid checking the error while unbinding the prior devmgr.
    //
    // We unbind the "real" dev if it was provided to us, which should only happen on the
    // first iteration of the test.
    fdio_ns_unbind(name_space, kDevPath);
    status = fdio_ns_bind_fd(name_space, kDevPath, isolated_devmgr.devfs_root().get());
    if (status != ZX_OK) {
      fprintf(stderr, "[FAILED]: Could not bind isolated devmgr into namespace: %d\n", status);
      exit(-1);
    }

    // Create a ramdisk within the new devmgr.
    if (ramdisk_create(disk.block_size, disk.block_count, &test_ramdisk) != ZX_OK) {
      fprintf(stderr, "[FAILED]: Could not create ramdisk for test\n");
      exit(-1);
    }

    test_disk_info.block_size = static_cast<uint32_t>(disk.block_size);
    test_disk_info.block_count = disk.block_count;
    strlcpy(test_disk_path, ramdisk_get_path(test_ramdisk), sizeof(test_disk_path));
  }

  if (test_class == FS_TEST_FVM) {
    fbl::unique_fd fd(open(test_disk_path, O_RDWR));
    if (!fd) {
      fprintf(stderr, "[FAILED]: Could not open test disk\n");
      exit(-1);
    }
    if (fvm_init(fd.get(), disk.slice_size) != ZX_OK) {
      fprintf(stderr, "[FAILED]: Could not format disk with FVM\n");
      exit(-1);
    }

    zx::channel fvm_channel;
    if (fdio_get_service_handle(fd.get(), fvm_channel.reset_and_get_address()) != ZX_OK) {
      fprintf(stderr, "[FAILED]: Could not convert fd to channel\n");
      exit(-1);
    }
    auto resp = ::llcpp::fuchsia::device::Controller::Call::Bind(
        zx::unowned_channel(fvm_channel.get()),
        ::fidl::StringView(FVM_DRIVER_LIB, (strlen(FVM_DRIVER_LIB))));
    zx_status_t status = resp.status();
    if (status == ZX_OK) {
      if (resp->result.is_err()) {
        status = resp->result.err();
      }
    }
    if (status != ZX_OK) {
      fprintf(stderr, "[FAILED]: Could not bind disk to FVM driver\n");
      exit(-1);
    }
    snprintf(fvm_disk_path, sizeof(fvm_disk_path), "%s/fvm", test_disk_path);
    if (wait_for_device(fvm_disk_path, ZX_SEC(3)) != ZX_OK) {
      fprintf(stderr, "[FAILED]: FVM driver never appeared at %s\n", test_disk_path);
      exit(-1);
    }

    // Open "fvm" driver
    fvm_channel.reset();
    fbl::unique_fd fvm_fd(open(fvm_disk_path, O_RDWR));
    if (!fvm_fd) {
      fprintf(stderr, "[FAILED]: Could not open FVM driver\n");
      exit(-1);
    }

    alloc_req_t request;
    memset(&request, 0, sizeof(request));
    request.slice_count = 1;
    strcpy(request.name, "fs-test-partition");
    memcpy(request.type, kTestPartGUID, sizeof(request.type));
    memcpy(request.guid, kTestUniqueGUID, sizeof(request.guid));

    fd.reset(fvm_allocate_partition(fvm_fd.get(), &request));
    if (!fd) {
      fprintf(stderr, "[FAILED]: Could not allocate FVM partition\n");
      exit(-1);
    }
    close(fvm_fd.release());

    fd.reset(open_partition(kTestUniqueGUID, kTestPartGUID, 0, test_disk_path));
    if (!fd) {
      fprintf(stderr, "[FAILED]: Could not locate FVM partition\n");
      exit(-1);
    }

    // Restore the "fvm_disk_path" to the containing disk, so it can
    // be destroyed when the test completes
    fvm_disk_path[strlen(fvm_disk_path) - strlen("/fvm")] = 0;
  }

  if (test_info->mkfs(test_disk_path)) {
    fprintf(stderr, "[FAILED]: Could not format disk (%s) for test\n", test_disk_path);
    exit(-1);
  }

  if (test_info->mount(test_disk_path, kMountPath)) {
    fprintf(stderr, "[FAILED]: Error mounting filesystem\n");
    exit(-1);
  }
}

void teardown_fs_test(fs_test_type_t test_class) {
  if (test_info->unmount(kMountPath)) {
    fprintf(stderr, "[FAILED]: Error unmounting filesystem\n");
    exit(-1);
  }

  if (test_info->fsck(test_disk_path)) {
    fprintf(stderr, "[FAILED]: Filesystem fsck failed\n");
    exit(-1);
  }

  if (test_class == FS_TEST_FVM) {
    if (use_real_disk) {
      if (fvm_destroy(fvm_disk_path) != ZX_OK) {
        fprintf(stderr, "[FAILED]: Couldn't destroy FVM on test disk\n");
        exit(-1);
      }
    }

    // Move the test_disk_path back to the 'real' disk, rather than
    // a partition within the FVM.
    strcpy(test_disk_path, fvm_disk_path);
  }

  if (!use_real_disk) {
    if (ramdisk_destroy(test_ramdisk)) {
      fprintf(stderr, "[FAILED]: Error destroying ramdisk\n");
      exit(-1);
    }

    fdio_ns_t* name_space;
    zx_status_t status = fdio_ns_get_installed(&name_space);
    if (status != ZX_OK) {
      fprintf(stderr, "[FAILED]: Could not acquire namespace\n");
      exit(-1);
    }
    status = fdio_ns_unbind(name_space, kDevPath);
    if (status != ZX_OK) {
      fprintf(stderr, "[FAILED]: Could not unbind isolated devmgr from namespace\n");
      exit(-1);
    }
  }
}

// FS-specific functionality:

template <const char* fs_name>
bool should_test_filesystem(void) {
  return !strcmp(filesystem_name_filter, "") || !strcmp(fs_name, filesystem_name_filter);
}

int mkfs_memfs(const char* disk_path) { return 0; }

int fsck_memfs(const char* disk_path) { return 0; }

// TODO(smklein): Even this hacky solution has a hacky implementation, and
// should be replaced with a variation of "rm -r" when ready.
static int unlink_recursive(const char* path) {
  DIR* dir;
  if ((dir = opendir(path)) == NULL) {
    return errno;
  }

  struct dirent* de;
  int r = 0;
  while ((de = readdir(dir)) != NULL) {
    if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
      continue;

    char tmp[PATH_MAX];
    tmp[0] = 0;
    size_t bytes_left = PATH_MAX - 1;
    strncat(tmp, path, bytes_left);
    bytes_left -= strlen(path);
    strncat(tmp, "/", bytes_left);
    bytes_left--;
    strncat(tmp, de->d_name, bytes_left);
    // At the moment, we don't have a great way of identifying what is /
    // isn't a directory. Just try to open it as a directory, and return
    // without an error if we're wrong.
    if ((r = unlink_recursive(tmp)) < 0) {
      break;
    }
    if ((r = unlink(tmp)) < 0) {
      break;
    }
  }

  closedir(dir);
  return r;
}

// TODO(smklein): It would be cleaner to unmount the filesystem completely,
// and remount a fresh copy. However, a hackier (but currently working)
// solution involves recursively deleting all files in the mounted
// filesystem.
int mount_memfs(const char* disk_path, const char* mount_path) {
  struct stat st;
  if (stat(kMountPath, &st)) {
    if (mkdir(kMountPath, 0644) < 0) {
      return -1;
    }
  } else if (!S_ISDIR(st.st_mode)) {
    return -1;
  }
  int r = unlink_recursive(kMountPath);
  return r;
}

int unmount_memfs(const char* mount_path) { return unlink_recursive(kMountPath); }

static int mkfs_common(const char* disk_path, disk_format_t fs_type) {
  zx_status_t status;
  if ((status = mkfs(disk_path, fs_type, launch_stdio_sync, &default_mkfs_options)) != ZX_OK) {
    fprintf(stderr, "Could not mkfs filesystem(%s)", disk_format_string(fs_type));
    return -1;
  }
  return 0;
}

static int fsck_common(const char* disk_path, disk_format_t fs_type) {
  zx_status_t status;
  if ((status = fsck(disk_path, fs_type, &test_fsck_options, launch_stdio_sync)) != ZX_OK) {
    fprintf(stderr, "fsck on %s failed", disk_format_string(fs_type));
    return -1;
  }
  return 0;
}

static int mount_common(const char* disk_path, const char* mount_path, disk_format_t fs_type) {
  fbl::unique_fd fd(open(disk_path, O_RDWR));

  if (!fd) {
    fprintf(stderr, "Could not open disk: %s\n", disk_path);
    return -1;
  }

  // fd consumed by mount. By default, mount waits until the filesystem is
  // ready to accept commands.
  zx_status_t status;
  if ((status = mount(fd.release(), mount_path, fs_type, &default_mount_options,
                      launch_stdio_async)) != ZX_OK) {
    fprintf(stderr, "Could not mount %s filesystem\n", disk_format_string(fs_type));
    return status;
  }

  return 0;
}

static int unmount_common(const char* mount_path) {
  zx_status_t status = umount(mount_path);
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to unmount filesystem\n");
    return status;
  }
  return 0;
}

int mkfs_minfs(const char* disk_path) { return mkfs_common(disk_path, DISK_FORMAT_MINFS); }

int fsck_minfs(const char* disk_path) { return fsck_common(disk_path, DISK_FORMAT_MINFS); }

int mount_minfs(const char* disk_path, const char* mount_path) {
  return mount_common(disk_path, mount_path, DISK_FORMAT_MINFS);
}

int unmount_minfs(const char* mount_path) { return unmount_common(mount_path); }

fs_info_t FILESYSTEMS[NUM_FILESYSTEMS] = {
    {
        memfs_name,
        should_test_filesystem<memfs_name>,
        mkfs_memfs,
        mount_memfs,
        unmount_memfs,
        fsck_memfs,
        .can_be_mounted = false,
        .can_mount_sub_filesystems = true,
        .supports_hardlinks = true,
        .supports_watchers = true,
        .supports_create_by_vmo = true,
        .supports_mmap = true,
        .supports_resize = false,
        .nsec_granularity = 1,
    },
    {
        minfs_name,
        should_test_filesystem<minfs_name>,
        mkfs_minfs,
        mount_minfs,
        unmount_minfs,
        fsck_minfs,
        .can_be_mounted = true,
        .can_mount_sub_filesystems = true,
        .supports_hardlinks = true,
        .supports_watchers = true,
        .supports_create_by_vmo = false,
        .supports_mmap = false,
        .supports_resize = true,
        .nsec_granularity = 1,
    },
};
