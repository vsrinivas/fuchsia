// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <lib/fdio/private.h>
#include <lib/zx/channel.h>
#include <lib/zx/socket.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <poll.h>
#include <sys/inotify.h>

#include <map>
#include <vector>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

#include "sdk/lib/fdio/cleanpath.h"
#include "sdk/lib/fdio/fdio_unistd.h"
#include "sdk/lib/fdio/internal.h"
#include "sdk/lib/fdio/zxio.h"

namespace {

// Inotify structure for individual watch descriptor, which is equivalent to a filter.
struct FdioInotifyWd {
  FdioInotifyWd(int input_filter_mask, int wd) {
    mask = input_filter_mask;
    watch_descriptor = wd;
  }
  int mask;
  int watch_descriptor;
  zx::channel client_request;
};

struct FdioInotify {
  FdioInotify(zxio_t zx_io, zx::socket client_end, zx::socket server_end)
      : io(zx_io), client(std::move(client_end)), server(std::move(server_end)) {
    filepath_to_filter = std::make_unique<std::map<std::string, std::unique_ptr<FdioInotifyWd>>>();
    watch_descriptors = std::make_unique<std::map<uint64_t, std::string>>();
  }
  zxio_t io;
  mtx_t lock = {};
  // The zx::socket object that is shared across all the filters for a single inotify instance.
  zx::socket client;
  // The zx::socket object that will be duplicated for different filters created.
  zx::socket server;

  // Monotonically increasing client-side watch descriptor generator. 0 value is reserved.
  int next_watch_descriptor __TA_GUARDED(lock) = 1;

  // Store filepath to watch desciptor mapping for identifying existing filters for a filepath.
  std::unique_ptr<std::map<std::string, std::unique_ptr<FdioInotifyWd>>> filepath_to_filter
      __TA_GUARDED(lock);

  // Store reverse lookup of watch descriptor to filepath for inotify_remove.
  std::unique_ptr<std::map<uint64_t, std::string>> watch_descriptors __TA_GUARDED(lock);
};

static_assert(sizeof(FdioInotify) <= sizeof(zxio_storage_t),
              "FdioInotify must fit inside zxio_storage_t.");

inline FdioInotify* zxio_to_inotify(zxio_t* zxio) { return reinterpret_cast<FdioInotify*>(zxio); }

zx_status_t inotify_close(zxio_t* io) {
  FdioInotify* inotify = reinterpret_cast<FdioInotify*>(io);
  inotify->~FdioInotify();
  return ZX_OK;
}

zx_status_t ionotify_get_read_buffer_available(zxio_t* io, size_t* out_available) {
  zx_info_socket_t info = {};
  zx_status_t status =
      zxio_to_inotify(io)->client.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    return status;
  }
  *out_available = info.rx_buf_available;
  return ZX_OK;
}

zx_status_t inotify_readv(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                          zxio_flags_t flags, size_t* out_actual) {
  if (flags) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  size_t total = fdio_iovec_get_capacity(vector, vector_count);

  std::vector<uint8_t> buf(total);

  size_t actual;
  zx_status_t status = zxio_to_inotify(io)->client.read(0, buf.data(), total, &actual);
  if (status != ZX_OK) {
    return status;
  }

  fdio_iovec_copy_to(buf.data(), actual, vector, vector_count, out_actual);
  return ZX_OK;
}

static void inotify_wait_begin(zxio_t* io, zxio_signals_t zxio_signals, zx_handle_t* out_handle,
                               zx_signals_t* out_zx_signals) {
  *out_handle = zxio_to_inotify(io)->client.get();

  zx_signals_t zx_signals = ZX_SIGNAL_NONE;
  if (zxio_signals & ZXIO_SIGNAL_READABLE) {
    zx_signals |= ZX_SOCKET_READABLE;
  }
  if (zxio_signals & ZXIO_SIGNAL_PEER_CLOSED) {
    zx_signals |= ZX_SOCKET_PEER_CLOSED;
  }
  *out_zx_signals = zx_signals;
}

static void inotify_wait_end(zxio_t* io, zx_signals_t zx_signals,
                             zxio_signals_t* out_zxio_signals) {
  zxio_signals_t zxio_signals = ZXIO_SIGNAL_NONE;
  if (zx_signals & ZX_SOCKET_READABLE) {
    zxio_signals |= ZXIO_SIGNAL_READABLE;
  }
  if (zx_signals & ZX_SOCKET_PEER_CLOSED) {
    zxio_signals |= ZXIO_SIGNAL_PEER_CLOSED;
  }
  *out_zxio_signals = zxio_signals;
}

constexpr zxio_ops_t inotify_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = inotify_close;
  ops.get_read_buffer_available = ionotify_get_read_buffer_available;
  ops.readv = inotify_readv;
  ops.wait_begin = inotify_wait_begin;
  ops.wait_end = inotify_wait_end;
  return ops;
}();

bool fdio_is_inotify(const fdio_ptr& io) {
  if (!io) {
    return false;
  }
  return zxio_get_ops(&io->zxio_storage().io) == &inotify_ops;
}

}  // namespace

__EXPORT
int inotify_init() { return inotify_init1(0); }

__EXPORT
int inotify_init1(int flags) {
  if (flags & ~(IN_CLOEXEC | IN_NONBLOCK)) {
    return ERRNO(EINVAL);
  }
  zx::result io = fdio_internal::zxio::create();
  if (io.is_error()) {
    return ERROR(io.status_value());
  }

  zx::socket client, server;

  // Create a common socket shared between all the filters in an inotify instance.
  // We need to avoid short writes for inotify events, and hence need to set
  // socket type as datagram.
  zx_status_t status = zx::socket::create(ZX_SOCKET_DATAGRAM, &client, &server);
  if (status != ZX_OK) {
    return ERROR(status);
  }

  auto inotify = new (&io->zxio_storage())
      FdioInotify(io->zxio_storage().io, std::move(client), std::move(server));
  zxio_init(&inotify->io, &inotify_ops);

  std::optional fd = bind_to_fd(io.value());
  if (fd.has_value()) {
    return fd.value();
  }
  return ERRNO(EMFILE);
}

__EXPORT
int inotify_add_watch(int fd, const char* pathname, uint32_t mask) {
  if (pathname == nullptr) {
    return ERRNO(EFAULT);
  }

  if (pathname[0] == '\0') {
    return ERRNO(ENOENT);
  }
  // canonicalize path and clean it on client-side.
  fdio_internal::PathBuffer buffer;
  bool has_ending_slash;
  bool cleaned = fdio_internal::CleanPath(pathname, &buffer, &has_ending_slash);
  if (!cleaned) {
    return ERRNO(ENAMETOOLONG);
  }
  std::string_view cleanpath(buffer);

  fdio_ptr iodir = fdio_iodir(AT_FDCWD, cleanpath);
  if (iodir == nullptr) {
    return ERRNO(EBADF);
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

  fdio_ptr io = fd_to_io(fd);
  if (!fdio_is_inotify(io)) {
    return ERRNO(EBADF);
  }

  FdioInotify* inotify = zxio_to_inotify(&io->zxio_storage().io);
  fbl::AutoLock lock(&inotify->lock);

  int watch_descriptor_to_use = inotify->next_watch_descriptor++;
  std::unique_ptr<FdioInotifyWd> wd =
      std::make_unique<FdioInotifyWd>(mask, watch_descriptor_to_use);

  zx::socket dup_server_socket_per_filter;
  zx_status_t status =
      inotify->server.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_server_socket_per_filter);
  if (status != ZX_OK) {
    return ERROR(status);
  }

  std::string cleanpath_copy(cleanpath);

  // Check if filter already exists and simply needs modification.
  auto res = inotify->filepath_to_filter->emplace(cleanpath_copy, std::move(wd));

  if (!res.second) {  // filter already present.
    uint32_t old_mask = res.first->second->mask;
    if (old_mask == mask || (mask & IN_MASK_CREATE)) {
      return ERRNO(EEXIST);
    }

    // remove wd to path mapping.
    auto old_watch_descriptor = res.first->second->watch_descriptor;
    inotify->watch_descriptors->erase(old_watch_descriptor);

    // Update filter.
    inotify->filepath_to_filter->insert({cleanpath_copy, std::move(wd)});
  }
  // Update watch_descriptor to filepath mapping.
  inotify->watch_descriptors->insert({watch_descriptor_to_use, std::move(cleanpath_copy)});

  status = iodir->add_inotify_filter(cleanpath, mask, watch_descriptor_to_use,
                                     std::move(dup_server_socket_per_filter));
  if (status != ZX_OK) {
    return ERROR(status);
  }

  return watch_descriptor_to_use;
}

__EXPORT
int inotify_rm_watch(int fd, int wd) {
  fdio_ptr io = fd_to_io(fd);
  if (!fdio_is_inotify(io)) {
    return ERRNO(EBADF);
  }

  FdioInotify* inotify = zxio_to_inotify(&io->zxio_storage().io);
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
  // FdioInotifyWd* wd_to_be_erased = iter_file->second.get();
  inotify->filepath_to_filter->erase(iter_file);

  return 0;
}
