// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_DRIVERS_HID_HID_FIFO_H_
#define SRC_UI_INPUT_DRIVERS_HID_HID_FIFO_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <threads.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

#ifndef HID_FIFO_SIZE
#define HID_FIFO_SIZE 4096
#endif
#define HID_FIFO_MASK (HID_FIFO_SIZE - 1)

typedef struct {
  uint8_t buf[HID_FIFO_SIZE];
  uint32_t head;
  uint32_t tail;
  bool empty;
} zx_hid_fifo_t;

zx_status_t zx_hid_fifo_create(zx_hid_fifo_t** fifo);
void zx_hid_fifo_init(zx_hid_fifo_t* fifo);
size_t zx_hid_fifo_size(zx_hid_fifo_t* fifo);
ssize_t zx_hid_fifo_peek(zx_hid_fifo_t* fifo, void* out);
ssize_t zx_hid_fifo_read(zx_hid_fifo_t* fifo, void* buf, size_t len);
ssize_t zx_hid_fifo_write(zx_hid_fifo_t* fifo, const void* buf, size_t len);

void zx_hid_fifo_dump(zx_hid_fifo_t* fifo);

__END_CDECLS

#endif  // SRC_UI_INPUT_DRIVERS_HID_HID_FIFO_H_
