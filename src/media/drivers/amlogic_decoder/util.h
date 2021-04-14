// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_UTIL_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_UTIL_H_

#include <lib/ddk/io-buffer.h>
#include <zircon/syscalls.h>

namespace amlogic_decoder {

inline void SetIoBufferName(io_buffer_t* buffer, const char* name) {
  zx_object_set_property(buffer->vmo_handle, ZX_PROP_NAME, name, strlen(name));
}

}  // namespace amlogic_decoder

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_UTIL_H_
