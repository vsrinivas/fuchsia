// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_UTIL_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_UTIL_H_

#include <zircon/syscalls.h>

#include <ddk/io-buffer.h>

inline void SetIoBufferName(io_buffer_t* buffer, const char* name) {
  zx_object_set_property(buffer->vmo_handle, ZX_PROP_NAME, name, strlen(name));
}

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_UTIL_H_
