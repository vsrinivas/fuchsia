// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/memfs/test/memfs_fs_test.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/namespace.h>
#include <lib/memfs/memfs.h>

#include "src/storage/fs_test/fs_test.h"

namespace {

class MemfsInstance : public fs_test::FilesystemInstance {
 public:
  MemfsInstance() : loop_(&kAsyncLoopConfigNeverAttachToThread) {
    ZX_ASSERT(loop_.StartThread() == ZX_OK);
  }
  ~MemfsInstance() override {
    if (fs_) {
      sync_completion_t sync;
      memfs_free_filesystem(fs_, &sync);
      ZX_ASSERT(sync_completion_wait(&sync, zx::duration::infinite().get()) == ZX_OK);
    }
  }
  zx::status<> Format(const fs_test::TestFilesystemOptions&) override {
    return zx::make_status(
        memfs_create_filesystem(loop_.dispatcher(), &fs_, root_.reset_and_get_address()));
  }

  zx::status<> Mount(const std::string& mount_path, const MountOptions& options) override {
    if (!root_) {
      // Already mounted.
      return zx::error(ZX_ERR_BAD_STATE);
    }
    fdio_ns_t* ns;
    if (auto status = zx::make_status(fdio_ns_get_installed(&ns)); status.is_error()) {
      return status;
    }
    return zx::make_status(
        fdio_ns_bind(ns, fs_test::StripTrailingSlash(mount_path).c_str(), root_.release()));
  }

  zx::status<> Unmount(const std::string& mount_path) override {
    return fs_test::FsUnbind(mount_path);
  }

  zx::status<> Fsck() override { return zx::ok(); }

  zx::status<std::string> DevicePath() const override { return zx::error(ZX_ERR_BAD_STATE); }

 private:
  async::Loop loop_;
  memfs_filesystem_t* fs_ = nullptr;
  zx::channel root_;  // Not valid after mounted.
};

class MemfsFilesystem : public fs_test::FilesystemImpl<MemfsFilesystem> {
 public:
  zx::status<std::unique_ptr<fs_test::FilesystemInstance>> Make(
      const fs_test::TestFilesystemOptions& options) const override {
    auto instance = std::make_unique<MemfsInstance>();
    zx::status<> status = instance->Format(options);
    if (status.is_error()) {
      return status.take_error();
    }
    return zx::ok(std::move(instance));
  }
  const Traits& GetTraits() const override {
    static Traits traits{
        .name = "memfs",
        .timestamp_granularity = zx::nsec(1),
        .supports_hard_links = true,
        .supports_mmap = true,
        .supports_resize = false,
        .max_file_size = 512 * 1024 * 1024,
        .in_memory = true,
        .is_case_sensitive = true,
        .supports_sparse_files = true,
        .is_journaled = false,
        .supports_fs_query = false,
        .supports_watch_event_deleted = false,
    };
    return traits;
  }
};

}  // namespace

namespace memfs {

fs_test::TestFilesystemOptions DefaultMemfsTestOptions() {
  return fs_test::TestFilesystemOptions{.description = "Memfs",
                                        .filesystem = &MemfsFilesystem::SharedInstance()};
}

}  // namespace memfs

__EXPORT std::unique_ptr<fs_test::Filesystem> GetFilesystem() {
  return std::make_unique<MemfsFilesystem>();
}
