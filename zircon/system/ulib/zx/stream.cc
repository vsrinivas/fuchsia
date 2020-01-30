// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/stream.h>
#include <zircon/syscalls.h>

namespace zx {

zx_status_t stream::create(uint32_t options, const vmo& vmo_handle, zx_off_t seek,
                           stream* out_stream) {
  // Assume |out_stream| and |vmo_handle| must refer to different containers, due
  // to strict aliasing.
  return zx_stream_create(options, vmo_handle.get(), seek, out_stream->reset_and_get_address());
}

}  // namespace zx
