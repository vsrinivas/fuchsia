// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <lib/zx/channel.h>
#include <lib/zx/socket.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <poll.h>
#include <sys/inotify.h>
#include <threads.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

#include "private.h"

namespace fio = ::llcpp::fuchsia::io;

namespace {

// Inotify structure for individual watch descriptor, which is equivalent to a filter.
struct NixInotifyWd {
  NixInotifyWd(int input_filter_mask, uint64_t wd) {
    mask = input_filter_mask;
    watch_descriptor = wd;
  }
  int mask;
  int watch_descriptor;
  zx::channel client_request;
};

struct NixInotify {
  NixInotify(zxio_t zx_io, zx::socket client_end, zx::socket server_end)
      : io(zx_io), client(std::move(client_end)), server(std::move(server_end)) {
    filepath_to_filter = std::make_unique<std::map<std::string, std::unique_ptr<NixInotifyWd>>>();
    watch_descriptors = std::make_unique<std::map<uint64_t, std::string>>();
  }
  zxio_t io;
  mtx_t lock = {};
  // The zx::socket object that is shared across all the filters for a single inotify instance.
  zx::socket client;
  zx::socket server;

  // Monotonically increasing client-side watch descriptor generator. 0 value is reserved.
  uint64_t next_watch_descriptor __TA_GUARDED(lock) = 1;

  // Store filepath to watch desciptor mapping for identifying existing filters for a filepath.
  std::unique_ptr<std::map<std::string, std::unique_ptr<NixInotifyWd>>> filepath_to_filter
      __TA_GUARDED(lock);

  // Store reverse lookup of watch descriptor to filepath for inotify_remove.
  std::unique_ptr<std::map<uint64_t, std::string>> watch_descriptors __TA_GUARDED(lock);
};

static_assert(sizeof(NixInotify) <= sizeof(zxio_storage_t),
              "NixInotify must fit inside zxio_storage_t.");

inline NixInotify* zxio_to_inotify(zxio_t* zxio) { return reinterpret_cast<NixInotify*>(zxio); }

zx_status_t inotify_close(zxio_t* io) {
  NixInotify* inotify = reinterpret_cast<NixInotify*>(io);
  inotify->~NixInotify();
  return ZX_OK;
}

zx_status_t inotify_readv(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                          zxio_flags_t flags, size_t* out_actual) {
  // TODO Implement readv.
  return ZX_ERR_NOT_SUPPORTED;
}

static constexpr zxio_ops_t inotify_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = inotify_close;
  ops.readv = inotify_readv;
  return ops;
}();

bool fdio_is_inotify(fdio_t* io) {
  if (!io) {
    return false;
  }
  return zxio_get_ops(fdio_get_zxio(io)) == &inotify_ops;
}

}  // namespace

__EXPORT
int inotify_init() { return inotify_init1(0); }

__EXPORT
int inotify_init1(int flags) {
  if (flags & ~(IN_CLOEXEC | IN_NONBLOCK)) {
    return ERRNO(EINVAL);
  }
  zxio_storage_t* storage = nullptr;
  fdio_t* fdio = fdio_zxio_create(&storage);
  if (fdio == nullptr) {
    return ERRNO(ENOMEM);
  }

  zx::socket client;
  zx::socket server;

  // Create a common socket shared between all the filters in an inotify instance.
  zx_status_t status = zx::socket::create(ZX_SOCKET_STREAM, &client, &server);
  if (status != ZX_OK) {
    fdio_unsafe_release(fdio);
    // TODO Use fdio_status_to_errno once inotify is moved to fdio.
    // Return generic error for now.
    return ERRNO(EIO);
  }

  auto inotify = new (storage) NixInotify(storage->io, std::move(client), std::move(server));
  zxio_init(&inotify->io, &inotify_ops);

  int fd = fdio_bind_to_fd(fdio, -1, 0);
  if (fd < 0) {
    fdio_unsafe_release(fdio);
    return ERRNO(ENOMEM);
  }
  return fd;
}

__EXPORT
int inotify_add_watch(int fd, const char* pathname, uint32_t mask) {
  if (pathname == nullptr) {
    return ERRNO(EFAULT);
  }

  if (pathname[0] == '\0') {
    return ERRNO(EINVAL);
  }

  std::string path = pathname;
  if (path.length() >= PATH_MAX) {
    return ERRNO(ENAMETOOLONG);
  }

  // TODO: Only include events which we will support initially.
  constexpr uint32_t allowed_events =
      IN_ACCESS | IN_MODIFY | IN_ATTRIB | IN_CLOSE_WRITE | IN_CLOSE_NOWRITE | IN_CLOSE | IN_OPEN |
      IN_MOVED_FROM | IN_MOVED_TO | IN_MOVE | IN_CREATE | IN_DELETE | IN_DELETE_SELF |
      IN_MOVE_SELF | IN_ALL_EVENTS | IN_UNMOUNT | IN_Q_OVERFLOW | IN_IGNORED | IN_ONLYDIR |
      IN_DONT_FOLLOW | IN_EXCL_UNLINK | IN_MASK_ADD | IN_ISDIR | IN_ONESHOT;
  if (mask & ~allowed_events) {
    return ERRNO(EINVAL);
  }

  // Mask cannot support IN_MASK_ADD and IN_MASK_CREATE together.
  if ((mask & (IN_MASK_ADD | IN_MASK_CREATE)) == (IN_MASK_ADD | IN_MASK_CREATE)) {
    return ERRNO(EINVAL);
  }

  fdio_t* io = fdio_unsafe_fd_to_io(fd);
  auto clean_io = fbl::MakeAutoCall([io] {
    if (io) {
      fdio_unsafe_release(io);
    }
  });
  if (!fdio_is_inotify(io)) {
    return ERRNO(EBADF);
  }

  NixInotify* inotify = zxio_to_inotify(fdio_get_zxio(io));
  fbl::AutoLock lock(&inotify->lock);

  int watch_descriptor_to_use = inotify->next_watch_descriptor++;
  std::unique_ptr<NixInotifyWd> wd = std::make_unique<NixInotifyWd>(mask, watch_descriptor_to_use);

  zx::socket dup_server_socket_per_filter;
  zx_status_t status =
      inotify->server.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_server_socket_per_filter);
  if (status != ZX_OK) {
    // TODO use fdio_status_to_errno once inotify is moved to fdio.
    // Return generic error for now.
    return ERRNO(EIO);
  }

  // Create a new inotify request channel for the filter.
  zx::channel server_request;
  status = zx::channel::create(0, &wd->client_request, &server_request);
  if (status != ZX_OK) {
    // TODO use fdio_status_to_errno once inotify is moved to fdio.
    // Return generic error for now.
    return ERRNO(EIO);
  }

  // Check if filter already exists and simply needs modification.
  auto res = inotify->filepath_to_filter->emplace(path, std::move(wd));

  if (!res.second) {  // filter already present.
    uint32_t old_mask = res.first->second->mask;
    if (old_mask == mask || (mask & IN_MASK_CREATE)) {
      return ERRNO(EEXIST);
    } else {
      // remove wd to path mapping.
      auto old_watch_descriptor = res.first->second->watch_descriptor;
      inotify->watch_descriptors->erase(old_watch_descriptor);

      // Update filter.
      inotify->filepath_to_filter->insert({path, std::move(wd)});
    }
  }
  // Update watch_descriptor to filepath mapping.
  inotify->watch_descriptors->insert({watch_descriptor_to_use, path});

  // TODO Call fidl fio::Directory::Call::AddInotifyFilter on current working directory to
  // update VFS side for this filter.

  return watch_descriptor_to_use;
}

__EXPORT
int inotify_rm_watch(int fd, int wd) {
  fdio_t* io = fdio_unsafe_fd_to_io(fd);
  auto clean_io = fbl::MakeAutoCall([io] {
    if (io) {
      fdio_unsafe_release(io);
    }
  });
  if (!fdio_is_inotify(io)) {
    return ERRNO(EBADF);
  }

  NixInotify* inotify = zxio_to_inotify(fdio_get_zxio(io));
  fbl::AutoLock lock(&inotify->lock);

  auto iter_wd = inotify->watch_descriptors->find(wd);

  // Filter not found or wd is not valid.
  if (iter_wd == inotify->watch_descriptors->end()) {
    return ERRNO(EINVAL);
  }

  std::string file_to_be_erased = iter_wd->second;
  inotify->watch_descriptors->erase(iter_wd);

  auto iter_file = inotify->filepath_to_filter->find(file_to_be_erased);
  // Filter not found or wd is not valid.
  if (iter_file == inotify->filepath_to_filter->end()) {
    return ERRNO(EINVAL);
  }

  // TODO : Close the channel for VFS to cleanup.
  // NixInotifyWd* wd_to_be_erased = iter_file->second.get();
  inotify->filepath_to_filter->erase(iter_file);

  return 0;
}
