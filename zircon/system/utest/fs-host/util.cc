// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include <minfs/fsck.h>

// Returns a mount path, preferring TMPDIR env var and falling back to /tmp.
std::string GetMountPath() {
  char const* kBase = getenv("TMPDIR");
  if (kBase == nullptr) {
    kBase = "/tmp";
  }
  return std::string(kBase).append("/zircon-fs-test");
}

void setup_fs_test(size_t disk_size) {
  std::string mount_path = GetMountPath();
  int r = open(mount_path.c_str(), O_RDWR | O_CREAT | O_EXCL, 0755);

  if (r < 0) {
    fprintf(stderr, "Unable to create disk for test filesystem at %s. \nMore Specifically: %s\n",
            mount_path.c_str(), strerror(errno));
    exit(-1);
  }

  if (ftruncate(r, disk_size) < 0) {
    fprintf(stderr, "Unable to truncate disk\n");
    exit(-1);
  }

  if (close(r) < 0) {
    fprintf(stderr, "Unable to close disk\n");
    exit(-1);
  }

  if (emu_mkfs(mount_path.c_str()) < 0) {
    fprintf(stderr, "Unable to run mkfs\n");
    exit(-1);
  }

  if (emu_mount(mount_path.c_str()) < 0) {
    fprintf(stderr, "Unable to run mount\n");
    exit(-1);
  }
}

void teardown_fs_test() {
  if (unlink(GetMountPath().c_str()) < 0) {
    exit(-1);
  }
}

int run_fsck() {
  fbl::unique_fd disk(open(GetMountPath().c_str(), O_RDONLY));

  if (!disk) {
    fprintf(stderr, "Unable to open disk for fsck\n");
    return -1;
  }

  struct stat stats;
  if (fstat(disk.get(), &stats) < 0) {
    fprintf(stderr, "error: minfs could not find end of file/device\n");
    return 0;
  }

  if (stats.st_size == 0) {
    fprintf(stderr, "Invalid disk\n");
    return -1;
  }

  size_t size = stats.st_size /= minfs::kMinfsBlockSize;

  std::unique_ptr<minfs::Bcache> block_cache;
  if (minfs::Bcache::Create(std::move(disk), static_cast<uint32_t>(size), &block_cache) != ZX_OK) {
    fprintf(stderr, "error: cannot create block cache\n");
    return -1;
  }

  // The filesystem is never repaired on the host side.
  return Fsck(std::move(block_cache), minfs::Repair::kDisabled);
}
