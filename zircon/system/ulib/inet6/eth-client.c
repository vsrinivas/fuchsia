// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "eth-client.h"

#include <fuchsia/hardware/ethernet/c/fidl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/device/ethernet.h>
#include <zircon/syscalls.h>

#if 0
#define IORING_TRACE(fmt...) fprintf(stderr, fmt)
#else
#define IORING_TRACE(fmt...) \
  do {                       \
  } while (0)
#endif

void eth_destroy(eth_client_t* eth) {
  zx_handle_close(eth->rx_fifo);
  zx_handle_close(eth->tx_fifo);
  free(eth);
}

zx_status_t eth_create(zx_handle_t svc, zx_handle_t io_vmo, void* io_mem, eth_client_t** out) {
  eth_client_t* eth;

  if ((eth = calloc(1, sizeof(*eth))) == NULL) {
    return ZX_ERR_NO_MEMORY;
  }

  fuchsia_hardware_ethernet_Fifos fifos;
  zx_status_t status, call_status;

  status = fuchsia_hardware_ethernet_DeviceGetFifos(svc, &call_status, &fifos);
  if (status != ZX_OK || call_status != ZX_OK) {
    fprintf(stderr, "eth_create: failed to get fifos: %d, %d\n", status, call_status);
    return status == ZX_OK ? call_status : status;
  }

  zx_handle_t vmo;
  if ((status = zx_handle_duplicate(io_vmo, ZX_RIGHT_SAME_RIGHTS, &vmo)) < 0) {
    fprintf(stderr, "eth_create: failed to duplicate vmo\n");
    goto fail;
  }
  status = fuchsia_hardware_ethernet_DeviceSetIOBuffer(svc, vmo, &call_status);
  if (status != ZX_OK || call_status != ZX_OK) {
    fprintf(stderr, "eth_create: failed to set iobuf: %d, %d\n", status, call_status);
    if (status == ZX_OK) {
      status = call_status;
    }
    goto fail;
  }
  status = fuchsia_hardware_ethernet_DeviceSetClientName(svc, "netsvc", 6, &call_status);
  if (status != ZX_OK || call_status != ZX_OK) {
    fprintf(stderr, "eth_create: failed to set client name %d, %d\n", status, call_status);
  }

  eth->tx_fifo = fifos.tx;
  eth->rx_fifo = fifos.rx;
  eth->rx_size = fifos.rx_depth;
  eth->tx_size = fifos.tx_depth;
  eth->iobuf = io_mem;

  *out = eth;
  return ZX_OK;

fail:
  zx_handle_close(fifos.tx);
  zx_handle_close(fifos.rx);
  eth_destroy(eth);
  return status;
}

zx_status_t eth_queue_tx(eth_client_t* eth, void* cookie, void* data, size_t len,
                         uint32_t options) {
  eth_fifo_entry_t e = {
      .offset = data - eth->iobuf,
      .length = len,
      .flags = options,
      .cookie = (uint64_t)cookie,
  };
  IORING_TRACE("eth:tx+ c=0x%08lx o=%u l=%u f=%u\n", e.cookie, e.offset, e.length, e.flags);

  return zx_fifo_write(eth->tx_fifo, sizeof(e), &e, 1, NULL);
}

zx_status_t eth_queue_rx(eth_client_t* eth, void* cookie, void* data, size_t len,
                         uint32_t options) {
  eth_fifo_entry_t e = {
      .offset = data - eth->iobuf,
      .length = len,
      .flags = options,
      .cookie = (uint64_t)cookie,
  };
  IORING_TRACE("eth:rx+ c=0x%08lx o=%u l=%u f=%u\n", e.cookie, e.offset, e.length, e.flags);
  return zx_fifo_write(eth->rx_fifo, sizeof(e), &e, 1, NULL);
}

zx_status_t eth_complete_tx(eth_client_t* eth, void* ctx, void (*func)(void* ctx, void* cookie)) {
  eth_fifo_entry_t entries[eth->tx_size];
  zx_status_t status;
  size_t count;
  if ((status = zx_fifo_read(eth->tx_fifo, sizeof(entries[0]), entries, countof(entries), &count)) <
      0) {
    if (status == ZX_ERR_SHOULD_WAIT) {
      return ZX_OK;
    } else {
      return status;
    }
  }

  for (eth_fifo_entry_t* e = entries; count-- > 0; e++) {
    IORING_TRACE("eth:tx- c=0x%08lx o=%u l=%u f=%u\n", e->cookie, e->offset, e->length, e->flags);
    func(ctx, (void*)e->cookie);
  }
  return ZX_OK;
}

zx_status_t eth_complete_rx(eth_client_t* eth, void* ctx,
                            void (*func)(void* ctx, void* cookie, size_t len, uint32_t flags)) {
  eth_fifo_entry_t entries[eth->rx_size];
  zx_status_t status;
  size_t count;
  if ((status = zx_fifo_read(eth->rx_fifo, sizeof(entries[0]), entries, countof(entries), &count)) <
      0) {
    if (status == ZX_ERR_SHOULD_WAIT) {
      return ZX_OK;
    } else {
      return status;
    }
  }

  for (eth_fifo_entry_t* e = entries; count-- > 0; e++) {
    IORING_TRACE("eth:rx- c=0x%08lx o=%u l=%u f=%u\n", e->cookie, e->offset, e->length, e->flags);
    func(ctx, (void*)e->cookie, e->length, e->flags);
  }
  return ZX_OK;
}

// Wait for completed rx packets
// ZX_ERR_PEER_CLOSED - far side disconnected
// ZX_ERR_TIMED_OUT - deadline lapsed
// ZX_OK - completed packets are available
zx_status_t eth_wait_rx(eth_client_t* eth, zx_time_t deadline) {
  zx_status_t status;
  zx_signals_t signals;

  if ((status = zx_object_wait_one(eth->rx_fifo, ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED, deadline,
                                   &signals)) < 0) {
    if (signals & ZX_FIFO_READABLE) {
      return ZX_OK;
    }
    return status;
  }
  if (signals & ZX_FIFO_PEER_CLOSED) {
    return ZX_ERR_PEER_CLOSED;
  }
  return ZX_OK;
}
