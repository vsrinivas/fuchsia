// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/wire/wire_types.h>
#include <zircon/assert.h>

namespace fidl::internal {

const CodingConfig kNullCodingConfig = {
    .max_iovecs_write = 1,
    .handle_metadata_stride = 0,
    .encode_process_handle = nullptr,
    .decode_process_handle = nullptr,
    .close = [](fidl_handle_t handle) { ZX_PANIC("Should not have handles"); },
    .close_many =
        [](const fidl_handle_t* handles, size_t num_handles) {
          ZX_ASSERT_MSG(num_handles == 0, "Should not have handles");
        },
};

}  // namespace fidl::internal
