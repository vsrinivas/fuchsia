// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <poll.h>
#include <sys/epoll.h>

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

int epoll_create(int size) { return ERRNO(ENOSYS); }

int epoll_create1(int flags) { return ERRNO(ENOSYS); }

int epoll_ctl(int epfd, int op, int fd, struct epoll_event* event) { return ERRNO(ENOSYS); }

int epoll_wait(int epfd, struct epoll_event* events, int maxevents, int timeout) {
  return ERRNO(ENOSYS);
}

int epoll_pwait(int epfd, struct epoll_event* events, int maxevents, int timeout,
                const sigset_t* sigmask) {
  return ERRNO(ENOSYS);
}
