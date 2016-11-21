// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#include <magenta/syscalls.h>
#include <magenta/types.h>

#include "apps/netstack/events.h"
#include "apps/netstack/handle_watcher.h"
#include "apps/netstack/iostate.h"
#include "apps/netstack/socket_functions.h"
#include "apps/netstack/trace.h"

iostate_t* iostate_alloc(void) {
  iostate_t* ios = calloc(1, sizeof(iostate_t));
  assert(ios);
  atomic_init(&ios->refcount, 1);
  ios->sockfd = -1;
  ios->data_h = MX_HANDLE_INVALID;
  debug_alloc("iostate_alloc: %p: rc=%d\n", ios, ios->refcount);
  return ios;
}

iostate_t* iostate_acquire(iostate_t* ios) {
  ios->refcount++;
  debug_alloc("iostate_acquire: %p: rc=%d\n", ios, ios->refcount);
  return ios;
}

void iostate_release(iostate_t* ios) {
  int refcount;
  refcount = (--ios->refcount);
  debug_alloc("iostate_release: %p: (%p %p) rc=%d\n", ios, ios->rbuf, ios->wbuf,
              ios->refcount);
  if (refcount == 0) {
    socket_signals_clear(ios, ios->watching_signals);
    if (ios->data_h != MX_HANDLE_INVALID) {
      debug_alloc("mx_handle_close: ios->data_h 0x%x (ios=%p)\n", ios->data_h,
                  ios);
      mx_handle_close(ios->data_h);
    }
    debug_alloc("iostate_release: %p: put rbuf %p\n", ios, ios->rbuf);
    put_rwbuf(ios->rbuf);
    debug_alloc("iostate_release: %p: put wbuf %p\n", ios, ios->wbuf);
    put_rwbuf(ios->wbuf);
    debug_alloc("iostate_release: %p: free ios\n", ios);
    free(ios);
  }
}
