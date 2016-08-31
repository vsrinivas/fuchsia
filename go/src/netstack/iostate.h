// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_NETSTACK_IOSTATE_H_
#define APPS_NETSTACK_IOSTATE_H_

#include <stdatomic.h>
#include <threads.h>

#include <magenta/types.h>

typedef struct rwbuf rwbuf_t;

typedef struct iostate {
  atomic_int refcount;
  int sockfd;
  mx_handle_t s;  // socket

  rwbuf_t* rbuf;
  int rlen;
  int roff;

  rwbuf_t* wbuf;
  int wlen;
  int woff;

  mx_signals_t watching_signals;

  // TRACE
  int read_net_read;
  int read_socket_write;
  int write_socket_read;
  int write_net_write;
} iostate_t;

iostate_t* iostate_alloc(void);
iostate_t* iostate_acquire(iostate_t* ios);
void iostate_release(iostate_t* ios);

#endif  // APPS_NETSTACK_IOSTATE_H_
