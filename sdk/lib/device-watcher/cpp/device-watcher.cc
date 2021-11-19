// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device-watcher.h"

#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/watcher.h>
#include <lib/zx/clock.h>
#include <string.h>

#include <fbl/unique_fd.h>

namespace device_watcher {

namespace {

constexpr char kDevPath[] = "/dev/";

}

namespace fio = fuchsia_io;

__EXPORT
zx_status_t DirWatcher::Create(fbl::unique_fd dir_fd,
                               std::unique_ptr<DirWatcher>* out_dir_watcher) {
  zx::channel client, server;
  zx_status_t status = zx::channel::create(0, &client, &server);
  if (status != ZX_OK) {
    return status;
  }
  fdio_cpp::FdioCaller caller(std::move(dir_fd));
  auto result = fidl::WireCall(fidl::UnownedClientEnd<fio::Directory>(caller.borrow_channel()))
                    ->Watch(fio::wire::kWatchMaskRemoved, 0, zx::channel(server.release()));
  if (!result.ok()) {
    return result.status();
  }
  *out_dir_watcher = std::make_unique<DirWatcher>(std::move(client));
  return ZX_OK;
}

__EXPORT
zx_status_t DirWatcher::WaitForRemoval(const fbl::String& filename, zx::duration timeout) {
  auto deadline = zx::deadline_after(timeout);
  // Loop until we see the removal event, or wait_one fails due to timeout.
  for (;;) {
    zx_signals_t observed;
    zx_status_t status = client_.wait_one(ZX_CHANNEL_READABLE, deadline, &observed);
    if (status != ZX_OK) {
      return status;
    }
    if (!(observed & ZX_CHANNEL_READABLE)) {
      return ZX_ERR_IO;
    }

    // Messages are of the form:
    //  uint8_t event
    //  uint8_t len
    //  char* name
    uint8_t buf[fio::wire::kMaxBuf];
    uint32_t actual_len;
    status = client_.read(0, buf, nullptr, sizeof(buf), 0, &actual_len, nullptr);
    if (status != ZX_OK) {
      return status;
    }
    if (buf[0] != fio::wire::kWatchEventRemoved) {
      continue;
    }
    if (filename.length() == 0) {
      // Waiting on any file.
      return ZX_OK;
    }
    if ((buf[1] == filename.length()) &&
        (memcmp(buf + 2, filename.c_str(), filename.length()) == 0)) {
      return ZX_OK;
    }
  }
  return ZX_ERR_NOT_FOUND;
}

// Waits for |file| to appear in |dir|, and opens it when it does.  Times out if
// the deadline passes.
__EXPORT
zx_status_t WaitForFile(const fbl::unique_fd& dir, const char* file, fbl::unique_fd* out) {
  auto watch_func = [](int dirfd, int event, const char* fn, void* cookie) -> zx_status_t {
    auto file = reinterpret_cast<const char*>(cookie);
    if (event != WATCH_EVENT_ADD_FILE) {
      return ZX_OK;
    }
    if (!strcmp(fn, file)) {
      return ZX_ERR_STOP;
    }
    return ZX_OK;
  };

  zx_status_t status =
      fdio_watch_directory(dir.get(), watch_func, ZX_TIME_INFINITE, const_cast<char*>(file));
  if (status != ZX_ERR_STOP) {
    return status;
  }
  out->reset(openat(dir.get(), file, O_RDWR));
  if (!out->is_valid()) {
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

namespace {

// This variant of WaitForFile opens the file specified relative to the rootdir,
// using the full_path. This is a workaround to deal with the fact that devhosts
// do not implement open_at.
zx_status_t WaitForFile2(const fbl::unique_fd& rootdir, const fbl::unique_fd& dir,
                         const char* full_path, const char* file, bool last, bool readonly,
                         fbl::unique_fd* out) {
  auto watch_func = [](int dirfd, int event, const char* fn, void* cookie) -> zx_status_t {
    auto file = reinterpret_cast<const char*>(cookie);
    if (event != WATCH_EVENT_ADD_FILE) {
      return ZX_OK;
    }
    if (!strcmp(fn, file)) {
      return ZX_ERR_STOP;
    }
    return ZX_OK;
  };

  zx_status_t status =
      fdio_watch_directory(dir.get(), watch_func, ZX_TIME_INFINITE, const_cast<char*>(file));
  if (status != ZX_ERR_STOP) {
    return status;
  }
  int flags = O_RDWR;
  if (readonly) {
    flags = O_RDONLY;
  }
  if (!last) {
    flags = O_RDONLY | O_DIRECTORY;
  }
  out->reset(openat(rootdir.get(), full_path, flags));
  if (!out->is_valid()) {
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

// Version of RecursiveWaitForFile that can mutate its path
zx_status_t RecursiveWaitForFileHelper(const fbl::unique_fd& rootdir, const fbl::unique_fd& dir,
                                       const char* full_path, char* path, bool readonly,
                                       fbl::unique_fd* out) {
  char* first_slash = strchr(path, '/');
  if (first_slash == nullptr) {
    // If there's no first slash, then we're just waiting for the file
    // itself to appear.
    return WaitForFile2(rootdir, dir, full_path, path, true, readonly, out);
  }
  *first_slash = 0;

  fbl::unique_fd next_dir;
  zx_status_t status = WaitForFile2(rootdir, dir, full_path, path, false, readonly, &next_dir);
  if (status != ZX_OK) {
    return status;
  }
  *first_slash = '/';
  return RecursiveWaitForFileHelper(rootdir, next_dir, full_path, first_slash + 1, readonly, out);
}

}  // namespace

// Waits for the relative |path| starting in |dir| to appear, and opens it.
__EXPORT
zx_status_t RecursiveWaitForFile(const fbl::unique_fd& dir, const char* path, fbl::unique_fd* out) {
  char path_copy[PATH_MAX];
  if (strlen(path) >= sizeof(path_copy)) {
    return ZX_ERR_INVALID_ARGS;
  }
  strcpy(path_copy, path);
  return RecursiveWaitForFileHelper(dir, dir, path_copy, path_copy, false, out);
}

__EXPORT
zx_status_t RecursiveWaitForFile(const char* path, fbl::unique_fd* out) {
  if (strncmp(kDevPath, path, strlen(kDevPath) - 1) != 0) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  fbl::unique_fd dev(open(kDevPath, O_RDONLY | O_DIRECTORY));
  return RecursiveWaitForFile(dev, path + strlen(kDevPath), out);
}

__EXPORT
zx_status_t RecursiveWaitForFileReadOnly(const char* path, fbl::unique_fd* out) {
  if (strncmp(kDevPath, path, strlen(kDevPath) - 1) != 0) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  fbl::unique_fd dev(open(kDevPath, O_RDONLY | O_DIRECTORY));
  return RecursiveWaitForFileReadOnly(dev, path + strlen(kDevPath), out);
}

__EXPORT
zx_status_t RecursiveWaitForFileReadOnly(const fbl::unique_fd& dir, const char* path,
                                         fbl::unique_fd* out) {
  char path_copy[PATH_MAX];
  if (strlen(path) >= sizeof(path_copy)) {
    return ZX_ERR_INVALID_ARGS;
  }
  strcpy(path_copy, path);
  return RecursiveWaitForFileHelper(dir, dir, path_copy, path_copy, true, out);
}

}  // namespace device_watcher
