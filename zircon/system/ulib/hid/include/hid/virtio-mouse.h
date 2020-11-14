// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HID_VIRTIO_MOUSE_H_
#define HID_VIRTIO_MOUSE_H_

#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

typedef struct hid_scroll_mouse_report {
  uint8_t button;
  int16_t rel_x;
  int16_t rel_y;
  int16_t rel_wheel;
} __PACKED hid_scroll_mouse_report_t;

// Returns a pointer to the static report descriptor array.
// The array is owned by this function.
const uint8_t* get_virtio_scroll_mouse_report_desc(size_t* out_size);

__END_CDECLS

#endif  // HID_VIRTIO_MOUSE_H_
