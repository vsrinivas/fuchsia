// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device-watcher.h"

#include <dirent.h>
#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/unsafe.h>
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
  zx::result endpoints = fidl::CreateEndpoints<fio::DirectoryWatcher>();
  if (endpoints.is_error()) {
    return endpoints.status_value();
  }

  fdio_t* io = fdio_unsafe_fd_to_io(dir_fd.get());
  zx::unowned_channel channel{fdio_unsafe_borrow_channel(io)};

  // Make a one-off call to fuchsia.io.Directory.Watch to open a channel from
  // which watch events can be read.
  auto result = fidl::WireCall(fidl::UnownedClientEnd<fio::Directory>(channel))
                    ->Watch(fio::wire::WatchMask::kRemoved, 0, std::move(endpoints->server));

  fdio_unsafe_release(io);
  if (!result.ok()) {
    return result.status();
  }
  *out_dir_watcher = std::make_unique<DirWatcher>(endpoints->client.TakeChannel());
  return ZX_OK;
}

__EXPORT
zx_status_t DirWatcher::WaitForRemoval(std::string_view filename, zx::duration timeout) {
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
    if (static_cast<fio::wire::WatchEvent>(buf[0]) != fio::wire::WatchEvent::kRemoved) {
      continue;
    }
    if (filename.length() == 0) {
      // Waiting on any file.
      return ZX_OK;
    }
    if ((buf[1] == filename.length()) &&
        (memcmp(buf + 2, filename.data(), filename.length()) == 0)) {
      return ZX_OK;
    }
  }
  return ZX_ERR_NOT_FOUND;
}

__EXPORT
zx_status_t IterateDirectory(fbl::unique_fd fd, FileCallback callback) {
  struct dirent* entry;

  DIR* dir = fdopendir(fd.get());
  if (dir == nullptr) {
    return ZX_ERR_IO;
  }

  fdio_t* io = fdio_unsafe_fd_to_io(fd.get());
  zx::unowned_channel dir_channel{fdio_unsafe_borrow_channel(io)};

  zx_status_t status = ZX_OK;
  while ((entry = readdir(dir)) != nullptr) {
    std::string filename(entry->d_name);
    if (filename == "." || filename == "..") {
      continue;
    }

    auto endpoints = fidl::CreateEndpoints<fio::Node>();
    if (endpoints.is_error()) {
      status = endpoints.status_value();
      goto finish;
    }

    // Open a channel to the file.
    status = fdio_open_at(dir_channel->get(), entry->d_name, 0,
                          endpoints->server.TakeChannel().release());
    if (status != ZX_OK) {
      goto finish;
    }

    // Invoke the user-provided callback.
    status = callback(filename, endpoints->client.TakeChannel());
    if (status != ZX_OK) {
      goto finish;
    }
  }

finish:
  fdio_unsafe_release(io);
  closedir(dir);
  return status;
}

// Waits for |file| to appear in |dir|, and opens it when it does.  Times out if
// the deadline passes.
__EXPORT
zx_status_t WaitForFile(const fbl::unique_fd& dir, const char* file, fbl::unique_fd* out) {
  auto watch_func = [](int dirfd, int event, const char* fn, void* cookie) -> zx_status_t {
    const std::string_view& file = *static_cast<std::string_view*>(cookie);
    if (event != WATCH_EVENT_ADD_FILE) {
      return ZX_OK;
    }
    if (std::string_view{fn} == file) {
      return ZX_ERR_STOP;
    }
    return ZX_OK;
  };

  std::string_view file_view{file};
  zx_status_t status = fdio_watch_directory(dir.get(), watch_func, ZX_TIME_INFINITE, &file_view);
  if (status != ZX_ERR_STOP) {
    return status;
  }
  out->reset(openat(dir.get(), file, O_RDWR));
  if (!out->is_valid()) {
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

__EXPORT
zx::result<zx::channel> WaitForDeviceTopologicalPath(const fbl::unique_fd& dir,
                                                     std::string_view topo_path) {
  struct TopoPathWatchState {
    std::string_view expected_topological_path;
    zx::channel out_device_channel;
  };

  auto watch_func = [](int dirfd, int event, const char* fn, void* cookie) -> zx_status_t {
    if (std::string_view{fn} == ".") {
      return ZX_OK;
    }

    if (event != WATCH_EVENT_ADD_FILE) {
      return ZX_OK;
    }

    auto dev_ends = fidl::CreateEndpoints<fuchsia_device::Controller>();
    if (dev_ends.is_error()) {
      return ZX_OK;
    }
    auto [dev_client_end, dev_server_end] = std::move(dev_ends.value());

    // Add new scope to limit use of unsafe variables.
    {
      fdio_t* io = fdio_unsafe_fd_to_io(dirfd);
      zx::unowned_channel channel{fdio_unsafe_borrow_channel(io)};

      zx_status_t status =
          fdio_service_connect_at(channel->get(), fn, dev_server_end.TakeChannel().release());
      fdio_unsafe_release(io);

      if (status != ZX_OK) {
        return ZX_OK;
      }
    }

    fidl::WireResult result = fidl::WireCall(dev_client_end)->GetTopologicalPath();
    if (!result.ok()) {
      return ZX_OK;
    }
    if (result->is_error()) {
      return ZX_OK;
    }

    auto state = static_cast<TopoPathWatchState*>(cookie);
    if (result->value()->path.get() == state->expected_topological_path) {
      state->out_device_channel = dev_client_end.TakeChannel();
      return ZX_ERR_STOP;
    }
    return ZX_OK;
  };

  TopoPathWatchState state = {.expected_topological_path = topo_path};

  zx_status_t status = fdio_watch_directory(dir.get(), watch_func, ZX_TIME_INFINITE, &state);
  if (status != ZX_ERR_STOP) {
    return zx::error(status);
  }

  return zx::ok(std::move(state.out_device_channel));
}

namespace {

// This variant of WaitForFile opens the file specified relative to the rootdir,
// using the full_path. This is a workaround to deal with the fact that devhosts
// do not implement open_at.
zx_status_t WaitForFile2(const fbl::unique_fd& rootdir, const fbl::unique_fd& dir,
                         const char* full_path, const char* file, bool last, bool readonly,
                         fbl::unique_fd* out) {
  auto watch_func = [](int dirfd, int event, const char* fn, void* cookie) -> zx_status_t {
    const std::string_view& file = *static_cast<std::string_view*>(cookie);
    if (event != WATCH_EVENT_ADD_FILE) {
      return ZX_OK;
    }
    if (std::string_view{fn} == file) {
      return ZX_ERR_STOP;
    }
    return ZX_OK;
  };

  std::string_view file_view{file};
  zx_status_t status = fdio_watch_directory(dir.get(), watch_func, ZX_TIME_INFINITE, &file_view);
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
