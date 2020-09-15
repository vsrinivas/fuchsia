// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdlib.h>
#include <zircon/status.h>

#include <ddk/device.h>
#include <ddk/protocol/serialimpl/async.h>

extern "C" {
// defined in rust
void serial_write_complete(void* ptr, zx_status_t status);

// defined in rust
void serial_read_complete(void* ptr, zx_status_t status, const void* buffer, size_t length);

void serial_write_async(void* serial, const void* buffer, size_t length, void* ptr) {
  serial_impl_async_write_async((serial_impl_async_protocol_t*)serial, buffer, length,
                                serial_write_complete, ptr);
}

void serial_read_async(void* serial, void* ptr) {
  serial_impl_async_read_async((serial_impl_async_protocol_t*)serial, serial_read_complete, ptr);
}

void serial_cancel_all(void* serial) {
  serial_impl_async_cancel_all((serial_impl_async_protocol_t*)serial);
}

void free_serial_impl_async_protocol(serial_impl_async_protocol_t* serial) { free(serial); }
}
