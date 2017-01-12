// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>

#include <mxio/dispatcher.h>
#include <mxio/io.h>

#include <magenta/syscalls.h>
#include <magenta/types.h>

// Don't include <sys/socket.h> in this file
#include "third_party/lwip/src/include/lwip/netdb.h"
#include "third_party/lwip/src/include/lwip/sockets.h"

#include "apps/netstack/events.h"
#include "apps/netstack/handle_watcher.h"
#include "apps/netstack/multiplexer.h"
#include "apps/netstack/request_queue.h"
#include "apps/netstack/socket_functions.h"
#include "apps/netstack/trace.h"

static fd_set s_active_read_set, s_active_write_set, s_active_except_set;
static int s_nwatch = 0;

void fd_event_set(int sockfd, int events) {
  if (events & EVENT_READ) FD_SET(sockfd, &s_active_read_set);
  if (events & EVENT_WRITE) FD_SET(sockfd, &s_active_write_set);
  if (events & EVENT_EXCEPT) FD_SET(sockfd, &s_active_except_set);
  if (s_nwatch < sockfd + 1) s_nwatch = sockfd + 1;
}

void fd_event_clear(int sockfd, int events) {
  if (events & EVENT_READ) FD_CLR(sockfd, &s_active_read_set);
  if (events & EVENT_WRITE) FD_CLR(sockfd, &s_active_write_set);
  if (events & EVENT_EXCEPT) FD_CLR(sockfd, &s_active_except_set);
  // TODO: can reduce s_nwatch?
}

int multiplexer(void *arg) {
  FD_ZERO(&s_active_read_set);
  FD_ZERO(&s_active_write_set);
  FD_ZERO(&s_active_except_set);

  int handle_watcher_fd;
  if (handle_watcher_init(&handle_watcher_fd) < 0) {
    error("multiplexer: handle_watcher is not ready\n");
    return -1;
  }
  fd_event_set(handle_watcher_fd, EVENT_READ);
  debug("handle_watcher_fd = %d\n", handle_watcher_fd);

  int request_fd = shared_queue_readfd();
  if (request_fd < 0) {
    error("multiplexer: shared_queue is not ready\n");
    return -1;
  }
  fd_event_set(request_fd, EVENT_READ);
  debug("request_fd = %d\n", request_fd);

  for (;;) {
    handle_watcher_start();

    fd_set read_set = s_active_read_set;
    fd_set write_set = s_active_write_set;
    fd_set except_set = s_active_except_set;

    vdebug("watching 0 to %d...\n", s_nwatch - 1);
    int nfd = lwip_select(s_nwatch, &read_set, &write_set, &except_set, NULL);
    // TODO: if nfd < 0?
    vdebug("nfd=%d\n", nfd);

    if (handle_watcher_stop() > 0) {
      handle_watcher_schedule_request();
    }

    if (FD_ISSET(handle_watcher_fd, &read_set)) {
      vdebug("handle_watcher_fd is set\n");
      --nfd;
      vdebug("multiplexer: clear interrupt\n");
      clear_interrupt(handle_watcher_fd);
    }

    if (FD_ISSET(request_fd, &read_set)) {
      vdebug("request_fd is set\n");
      --nfd;
      request_t *rq;
      rq = shared_queue_get();
      if (rq == NULL) {
        error("shared queue is empty?\n");
        break;
      } else {
        handle_request(rq, EVENT_NONE, MX_SIGNAL_NONE);
      }
    }

    for (int i = 0; i < s_nwatch; i++) {
      if (nfd == 0) break;
      if (i == request_fd || i == handle_watcher_fd) continue;

      int events = 0;
      if (FD_ISSET(i, &read_set)) {
        --nfd;
        debug("fd %d is readable\n", i);
        events |= EVENT_READ;
        FD_CLR(i, &s_active_read_set);
      }
      if (FD_ISSET(i, &write_set)) {
        --nfd;
        debug("fd %d is writable\n", i);
        events |= EVENT_WRITE;
        FD_CLR(i, &s_active_write_set);
      }
      if (FD_ISSET(i, &except_set)) {
        --nfd;
        debug("fd %d has an exception\n", i);
        events |= EVENT_EXCEPT;
        FD_CLR(i, &s_active_except_set);
      }
      if (events) {
        request_queue_t q;
        request_queue_init(&q);
        wait_queue_swap(WAIT_NET, i, &q);

        request_t *rq;
        while ((rq = request_queue_get(&q)) != NULL) {
          handle_request(rq, events, MX_SIGNAL_NONE);
        }
      }
    }
  }  // end of for (;;)

  return 0;
}

mx_status_t interrupter_create(int *sender_out, int *receiver_out) {
  if (sender_out == NULL || receiver_out == NULL) return ERR_INVALID_ARGS;

  int acceptor = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (acceptor < 0) return ERR_IO;

  int opt = 1;
  if (lwip_setsockopt(acceptor, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) <
      0)
    return ERR_IO;

  struct sockaddr_in addr;
  socklen_t addrlen = sizeof(addr);

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;

  if (lwip_bind(acceptor, (struct sockaddr *)&addr, addrlen) < 0) return ERR_IO;

  if (lwip_getsockname(acceptor, (struct sockaddr *)&addr, &addrlen) < 0)
    return ERR_IO;

  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

#define SOMAXCONN 128

  if (lwip_listen(acceptor, SOMAXCONN) < 0) return ERR_IO;

  int sender = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sender < 0) return ERR_IO;

  if (lwip_connect(sender, (struct sockaddr *)&addr, addrlen) < 0)
    return ERR_IO;

  int receiver = lwip_accept(acceptor, NULL, NULL);
  if (receiver < 0) return ERR_IO;

  lwip_close(acceptor);

  int non_blocking = 1;
  if (lwip_ioctl(sender, FIONBIO, &non_blocking) < 0) return ERR_IO;

  opt = 1;
  if (lwip_setsockopt(sender, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0)
    return ERR_IO;

  non_blocking = 1;
  if (lwip_ioctl(receiver, FIONBIO, &non_blocking) < 0) return ERR_IO;

  opt = 1;
  if (lwip_setsockopt(receiver, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) <
      0)
    return ERR_IO;

  *sender_out = sender;
  *receiver_out = receiver;

  return 0;
}

mx_status_t send_interrupt(int sender) {
  char byte = 1;
  int n = lwip_write(sender, &byte, 1);
  if (n < 0) {
    error("send_interrupt(fd=%d): lwip_write failed (errno=%d)\n", sender,
          errno);
    return ERR_IO;
  } else if (n != 1) {
    error("send_interrupt(fd=%d): lwip_write returned %d\n", sender, n);
    return ERR_IO;
  }
  return NO_ERROR;
}

mx_status_t clear_interrupt(int receiver) {
  char byte;
  int n = lwip_read(receiver, &byte, 1);
  if (n < 0) {
    error("clear_interrupt(fd=%d): lwip_read failed (errno=%d)\n", receiver,
          errno);
    return ERR_IO;
  } else if (n != 1) {
    error("clear_interrupt(fd=%d): lwip_read returned %d\n", receiver, n);
    return ERR_IO;
  }
  return NO_ERROR;
}
