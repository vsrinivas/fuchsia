// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <poll.h>
#include <sys/epoll.h>
#include <threads.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>

#include <unordered_map>
#include <vector>

#include <fbl/auto_call.h>
#include <fbl/mutex.h>

#include "private.h"

// We use a subset of the EPOLL* macros as canonical ways to
// express types of events to wait for in fdio, and depend on the
// corresponding POLL* matching them.
static_assert(EPOLLIN == POLLIN, "");
static_assert(EPOLLPRI == POLLPRI, "");
static_assert(EPOLLOUT == POLLOUT, "");
static_assert(EPOLLRDNORM == POLLRDNORM, "");
static_assert(EPOLLRDBAND == POLLRDBAND, "");
static_assert(EPOLLWRNORM == POLLWRNORM, "");
static_assert(EPOLLWRBAND == POLLWRBAND, "");
static_assert(EPOLLMSG == POLLMSG, "");
static_assert(EPOLLERR == POLLERR, "");
static_assert(EPOLLHUP == POLLHUP, "");
static_assert(EPOLLRDHUP == POLLRDHUP, "");

struct nix_epoll_fd {
  nix_epoll_fd(epoll_event* ev) {
    handle = ZX_HANDLE_INVALID;
    signals = ZXIO_SIGNAL_NONE;
    event = *ev;
  }
  epoll_event event;
  zx_handle_t handle; // a borrowed handle from fdio_unsafe_wait_begin
  zx_signals_t signals;
};

struct nix_epoll {
  zxio_t io;
  zx_handle_t port;
  mtx_t lock;
  std::unordered_map<uint64_t, nix_epoll_fd>* fd_to_event;
  std::vector<uint64_t>* inactive_fds;
};

static_assert(sizeof(nix_epoll) <= sizeof(zxio_storage_t),
              "nix_epoll must fit inside zxio_storage_t.");

inline nix_epoll* zxio_to_epoll(zxio_t* zxio) { return reinterpret_cast<nix_epoll*>(zxio); }

bool epoll_remove_fd(zxio_t* io, int fd) {
  nix_epoll* epoll = zxio_to_epoll(io);
  auto pos = epoll->fd_to_event->find(fd);
  if (pos == epoll->fd_to_event->end()) {
    return false;
  }
  zx_port_cancel(epoll->port, pos->second.handle, static_cast<uint64_t>(fd));
  epoll->fd_to_event->erase(pos);
  return true;
}

zx_status_t epoll_close(zxio_t* io) {
  nix_epoll* epoll = zxio_to_epoll(io);
  for (auto it = epoll->fd_to_event->begin(); it != epoll->fd_to_event->end(); ++it) {
    zx_port_cancel(epoll->port, it->second.handle, it->first);
  }
  delete epoll->fd_to_event;
  delete epoll->inactive_fds;
  zx_handle_close(epoll->port);

  return ZX_OK;
}

static constexpr zxio_ops_t epoll_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = epoll_close;
  return ops;
}();

bool fdio_is_epoll(fdio_t* io) {
  if (!io) {
    return false;
  }
  return zxio_get_ops(fdio_get_zxio(io)) == &epoll_ops;
}

__EXPORT
int epoll_create(int size) {
  if (size < 1) {
    return ERRNO(EINVAL);
  }
  return epoll_create1(0);
}

__EXPORT
int epoll_create1(int flags) {
  // |flags| is unused as the only valid value is EPOLL_CLOEXEC
  // which is meaningless, since there is no exec on Fuchsia.
  // Do not throw an error if specified, as exising code
  // will use this.
  zxio_storage_t* storage = nullptr;
  fdio_t* fdio = fdio_zxio_create(&storage);
  if (fdio == nullptr) {
    return ERRNO(ENOMEM);
  }

  nix_epoll* epoll = reinterpret_cast<nix_epoll*>(storage);
  zxio_init(&epoll->io, &epoll_ops);
  epoll->port = ZX_HANDLE_INVALID;
  epoll->fd_to_event = new std::unordered_map<uint64_t, nix_epoll_fd>;
  epoll->inactive_fds = new std::vector<uint64_t>;
  epoll->lock = MTX_INIT;

  zx_status_t status = zx_port_create(0, &epoll->port);
  if (status != ZX_OK) {
    fdio_unsafe_release(fdio);
    return ERRNO(ENOMEM);
  }
  int fd = fdio_bind_to_fd(fdio, -1, 0);
  if (fd < 0) {
    fdio_unsafe_release(fdio);
    return ERRNO(ENOMEM);
  }
  return fd;
}

__EXPORT
int epoll_ctl(int epfd, int op, int fd, struct epoll_event* event) {
  if (op != EPOLL_CTL_ADD && op != EPOLL_CTL_MOD && op != EPOLL_CTL_DEL) {
    return ERRNO(EINVAL);
  }
  // These are the only flags supported in fdio_zxio_wait_begin,
  // fdio_zxio_remote_wait_begin and poll_events_to_zxio_signals
  const uint32_t allowed_events = EPOLLIN | EPOLLPRI | EPOLLOUT | EPOLLERR | EPOLLHUP | EPOLLRDHUP;
  if (event->events & ~allowed_events) {
    return ERRNO(ENOTSUP);
  }

  fdio_t* io = fdio_unsafe_fd_to_io(epfd);
  auto clean_io = fbl::MakeAutoCall([io] {
    if (io) {
      fdio_unsafe_release(io);
    }
  });
  if (!fdio_is_epoll(io)) {
    return ERRNO(EBADF);
  }

  nix_epoll* epoll = zxio_to_epoll(fdio_get_zxio(io));
  mtx_lock(&epoll->lock);

  fdio_t* pollio = fdio_unsafe_fd_to_io(fd);
  if (pollio == nullptr) {
    mtx_unlock(&epoll->lock);
    return ERRNO(EBADF);
  }
  auto clean_io_poll = fbl::MakeAutoCall([pollio] { fdio_unsafe_release(pollio); });

  if (op == EPOLL_CTL_MOD || op == EPOLL_CTL_DEL) {
    if (!epoll_remove_fd(fdio_get_zxio(io), fd)) {
      mtx_unlock(&epoll->lock);
      return ERRNO(ENOENT);
    }
  }

  if (op == EPOLL_CTL_MOD || op == EPOLL_CTL_ADD) {
    uint64_t key = static_cast<uint64_t>(fd);
    auto res = epoll->fd_to_event->emplace(key, nix_epoll_fd(event));
    if (!res.second) {
      mtx_unlock(&epoll->lock);
      return ERRNO(EEXIST);
    }

    fdio_unsafe_wait_begin(pollio, event->events, &res.first->second.handle,
                           &res.first->second.signals);
    zx_status_t status = zx_object_wait_async(res.first->second.handle, epoll->port, key,
                                              res.first->second.signals, 0);
    if (status != ZX_OK) {
      mtx_unlock(&epoll->lock);
      return ERRNO(EINVAL);
    }
  }

  mtx_unlock(&epoll->lock);
  return 0;
}

__EXPORT
int epoll_wait(int epfd, struct epoll_event* events, int maxevents, int timeout) {
  if (maxevents < 1) {
    return ERRNO(EINVAL);
  }
  fdio_t* io = fdio_unsafe_fd_to_io(epfd);
  auto clean_io = fbl::MakeAutoCall([io] {
    if (io) {
      fdio_unsafe_release(io);
    }
  });
  if (!fdio_is_epoll(io)) {
    return ERRNO(EBADF);
  }

  zx_time_t deadline = ZX_TIME_INFINITE;
  if (timeout >= 0) {
    deadline = zx_deadline_after(zx_duration_from_msec(timeout));
  } else if (timeout != -1) {
    return ERRNO(EINVAL);
  }

  nix_epoll* epoll = zxio_to_epoll(fdio_get_zxio(io));

  mtx_lock(&epoll->lock);
  bool wait_err = false;
  for (auto it = epoll->inactive_fds->begin(); it != epoll->inactive_fds->end(); ++it) {
    uint64_t fd = *it;
    auto evinfo = epoll->fd_to_event->find(fd);
    if (evinfo != epoll->fd_to_event->end()) {
      fdio_t* pollio = fdio_unsafe_fd_to_io(static_cast<int>(fd));
      if (pollio) {
        fdio_unsafe_wait_begin(pollio, evinfo->second.event.events, &evinfo->second.handle,
                               &evinfo->second.signals);
        zx_status_t status =
            zx_object_wait_async(evinfo->second.handle, epoll->port, fd, evinfo->second.signals, 0);
        fdio_unsafe_release(pollio);
        if (status != ZX_OK) {
          wait_err = true;
        }
      }
    }
  }
  epoll->inactive_fds->clear();
  mtx_unlock(&epoll->lock);
  if (wait_err) {
    return ERRNO(EINVAL);
  }

  // Ideally we should have a means of waiting on the port that
  // that can return a vector of packets that are ready.
  std::vector<zx_port_packet_t> packets;
  zx_port_packet_t packet;
  zx_status_t status = zx_port_wait(epoll->port, deadline, &packet);
  if (status == ZX_OK) {
    packets.push_back(packet);
  }
  while (status == ZX_OK && packets.size() < static_cast<unsigned long>(maxevents)) {
    status = zx_port_wait(epoll->port, ZX_TIME_INFINITE_PAST, &packet);
    if (status == ZX_OK) {
      packets.push_back(packet);
    }
  }

  mtx_lock(&epoll->lock);

  int ready_count = 0;
  if (status == ZX_OK || status == ZX_ERR_TIMED_OUT) {
    for (auto pkt : packets) {
      // The packet type produced by zx_object_wait_async.
      if (pkt.type == ZX_PKT_TYPE_SIGNAL_ONE) {
        // |pkt.key| is the file descriptor.
        int fd = static_cast<int>(pkt.key);
        fdio_t* pollio = fdio_unsafe_fd_to_io(fd);
        if (pollio) {
          uint32_t evs;
          fdio_unsafe_wait_end(pollio, pkt.signal.observed, &evs);
          fdio_unsafe_release(pollio);
          auto evinfo = epoll->fd_to_event->find(fd);
          // This file descriptor could have been removed
          // with epoll_ctl/EPOLL_CTL_DEL, so it will not
          // be in the |fd_to_event| map. Ignore it.
          if (evinfo != epoll->fd_to_event->end()) {
            events[ready_count].events = evs;
            events[ready_count].data = evinfo->second.event.data;
            epoll->inactive_fds->push_back(pkt.key);
            ready_count++;
          }
        }
      }
    }
  }

  mtx_unlock(&epoll->lock);

  if (status != ZX_OK && status != ZX_ERR_TIMED_OUT) {
    return ERRNO(EINVAL);
  }

  return ready_count;
}

__EXPORT
int epoll_pwait(int epfd, struct epoll_event* events, int maxevents, int timeout,
                const sigset_t* sigmask) {
  return ERRNO(ENOSYS);
}
