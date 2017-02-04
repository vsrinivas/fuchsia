// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netstack/ports/lwip/eth-client.h"

#include <magenta/syscalls.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if 0
#define IORING_TRACE(fmt...) fprintf(stderr, fmt)
#else
#define IORING_TRACE(fmt...) \
  do {                       \
  } while (0)
#endif

void eth_destroy(eth_client_t* eth) {
  mx_handle_close(eth->rx_fifo);
  mx_handle_close(eth->tx_fifo);
  free(eth);
}

mx_status_t eth_create(int fd,
                       mx_handle_t io_vmo,
                       void* io_mem,
                       eth_client_t** out) {
  eth_client_t* eth;

  if ((eth = calloc(1, sizeof(*eth))) == NULL) {
    return ERR_NO_MEMORY;
  }

  eth_fifos_t fifos;
  mx_status_t status;

  ssize_t r;
  if ((r = ioctl_ethernet_get_fifos(fd, &fifos)) < 0) {
    fprintf(stderr, "eth_create: failed to get fifos: %zd\n", r);
    return r;
  }

  mx_handle_t vmo;
  if ((status = mx_handle_duplicate(io_vmo, MX_RIGHT_SAME_RIGHTS, &vmo)) < 0) {
    fprintf(stderr, "eth_create: failed to duplicate vmo\n");
    goto fail;
  }
  if ((r = ioctl_ethernet_set_iobuf(fd, &vmo)) < 0) {
    fprintf(stderr, "eth_create: failed to set iobuf: %zd\n", r);
    status = r;
    goto fail;
  }

  eth->tx_fifo = fifos.tx_fifo;
  eth->rx_fifo = fifos.rx_fifo;
  eth->rx_size = fifos.rx_depth;
  eth->tx_size = fifos.tx_depth;
  eth->iobuf = io_mem;

  *out = eth;
  return NO_ERROR;

fail:
  mx_handle_close(fifos.tx_fifo);
  mx_handle_close(fifos.rx_fifo);
  eth_destroy(eth);
  return status;
}

mx_status_t eth_queue_tx(eth_client_t* eth,
                         void* cookie,
                         void* data,
                         size_t len,
                         uint32_t options) {
  eth_fifo_entry_t e = {
      .offset = data - eth->iobuf,
      .length = len,
      .flags = options,
      .cookie = cookie,
  };
  uint32_t actual;
  IORING_TRACE("eth:tx+ c=%p o=%u l=%u f=%u\n", e.cookie, e.offset, e.length,
               e.flags);
  return mx_fifo_write(eth->tx_fifo, &e, sizeof(e), &actual);
}

mx_status_t eth_queue_rx(eth_client_t* eth,
                         void* cookie,
                         void* data,
                         size_t len,
                         uint32_t options) {
  eth_fifo_entry_t e = {
      .offset = data - eth->iobuf,
      .length = len,
      .flags = options,
      .cookie = cookie,
  };
  uint32_t actual;
  IORING_TRACE("eth:rx+ c=%p o=%u l=%u f=%u\n", e.cookie, e.offset, e.length,
               e.flags);
  return mx_fifo_write(eth->rx_fifo, &e, sizeof(e), &actual);
}

mx_status_t eth_complete_tx(eth_client_t* eth,
                            void* ctx,
                            void (*func)(void* ctx, void* cookie)) {
  eth_fifo_entry_t entries[eth->tx_size];
  mx_status_t status;
  uint32_t count;
  if ((status = mx_fifo_read(eth->tx_fifo, entries, sizeof(entries), &count)) <
      0) {
    if (status == ERR_SHOULD_WAIT) {
      return NO_ERROR;
    } else {
      return status;
    }
  }

  for (eth_fifo_entry_t* e = entries; count-- > 0; e++) {
    IORING_TRACE("eth:tx- c=%p o=%u l=%u f=%u\n", e->cookie, e->offset,
                 e->length, e->flags);
    func(ctx, e->cookie);
  }
  return NO_ERROR;
}

mx_status_t eth_complete_rx(
    eth_client_t* eth,
    void* ctx,
    void (*func)(void* ctx, void* cookie, size_t len, uint32_t flags)) {
  eth_fifo_entry_t entries[eth->rx_size];
  mx_status_t status;
  uint32_t count;
  if ((status = mx_fifo_read(eth->rx_fifo, entries, sizeof(entries), &count)) <
      0) {
    if (status == ERR_SHOULD_WAIT) {
      return NO_ERROR;
    } else {
      return status;
    }
  }

  for (eth_fifo_entry_t* e = entries; count-- > 0; e++) {
    IORING_TRACE("eth:rx- c=%p o=%u l=%u f=%u\n", e->cookie, e->offset,
                 e->length, e->flags);
    func(ctx, e->cookie, e->length, e->flags);
  }
  return NO_ERROR;
}

// Wait for completed rx packets
// ERR_REMOTE_CLOSED - far side disconnected
// ERR_TIMED_OUT - timeout expired
// NO_ERROR - completed packets are available
mx_status_t eth_wait_rx(eth_client_t* eth, mx_time_t timeout) {
  mx_status_t status;
  mx_signals_t signals;

  if ((status = mx_object_wait_one(eth->rx_fifo,
                                   MX_FIFO_READABLE | MX_FIFO_PEER_CLOSED,
                                   timeout, &signals)) < 0) {
    return status;
  }
  if (signals & MX_FIFO_PEER_CLOSED) {
    return ERR_REMOTE_CLOSED;
  }
  return NO_ERROR;
}
