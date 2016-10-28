// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <poll.h>
#include <sys/epoll.h>

// We use a subset of the EPOLL* macros as canonical ways to
// express types of events to wait for in mxio, and depend on the
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
