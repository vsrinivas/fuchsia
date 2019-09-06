// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hid-fifo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define min(a, b) ((a) < (b) ? (a) : (b))

zx_status_t zx_hid_fifo_create(zx_hid_fifo_t** fifo) {
  *fifo = malloc(sizeof(zx_hid_fifo_t));
  if (*fifo == NULL)
    return ZX_ERR_NO_MEMORY;
  zx_hid_fifo_init(*fifo);
  return ZX_OK;
}

void zx_hid_fifo_init(zx_hid_fifo_t* fifo) {
  memset(fifo->buf, 0, HID_FIFO_SIZE);
  fifo->head = fifo->tail = 0;
  fifo->empty = true;
}

size_t zx_hid_fifo_size(zx_hid_fifo_t* fifo) {
  if (fifo->empty)
    return 0;
  if (fifo->head > fifo->tail)
    return fifo->head - fifo->tail;
  return HID_FIFO_SIZE - fifo->tail + fifo->head;
}

ssize_t zx_hid_fifo_peek(zx_hid_fifo_t* fifo, void* out) {
  if (fifo->empty)
    return 0;
  *(uint8_t*)out = fifo->buf[fifo->tail];
  return 1;
}

ssize_t zx_hid_fifo_read(zx_hid_fifo_t* fifo, void* buf, size_t len) {
  if (!buf)
    return ZX_ERR_INVALID_ARGS;
  if (fifo->empty)
    return 0;
  if (!len)
    return 0;

  len = min(zx_hid_fifo_size(fifo), len);
  for (size_t c = len; c > 0; c--, fifo->tail = (fifo->tail + 1) & HID_FIFO_MASK) {
    *(uint8_t*)buf++ = fifo->buf[fifo->tail];
  }
  if (fifo->tail == fifo->head)
    fifo->empty = true;
  return len;
}

ssize_t zx_hid_fifo_write(zx_hid_fifo_t* fifo, const void* buf, size_t len) {
  if (!fifo->empty && fifo->tail == fifo->head)
    return ZX_ERR_BUFFER_TOO_SMALL;
  if (len > HID_FIFO_SIZE - zx_hid_fifo_size(fifo))
    return ZX_ERR_BUFFER_TOO_SMALL;

  for (size_t c = len; c > 0; c--, fifo->head = (fifo->head + 1) & HID_FIFO_MASK) {
    fifo->buf[fifo->head] = *(uint8_t*)buf++;
  }
  fifo->empty = false;
  return len;
}

void zx_hid_fifo_dump(zx_hid_fifo_t* fifo) {
  printf("zx_hid_fifo_dump %p\n", fifo);
  printf("head: %u  tail: %u  empty: %s\n", fifo->head, fifo->tail, fifo->empty ? "Y" : "N");
  if (fifo->empty) {
    return;
  }
  uint32_t c = fifo->tail;
  int i = 0;
  do {
    printf("%02x ", fifo->buf[c]);
    if (i++ % 8 == 7)
      printf("\n");
    c = (c + 1) & HID_FIFO_MASK;
  } while (c != fifo->head);
  printf("\n");
}
