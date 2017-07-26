// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/coding.h>

#include <zircon/types.h>

zx_status_t fidl_encode(const fidl_type_t* type,
                        void* bytes,
                        size_t num_bytes,
                        zx_handle_t* handles,
                        size_t num_handles,
                        size_t* actual_handles_out,
                        const char** error_msg_out) {
    // TODO(kulakowski)
    return ZX_ERR_NOT_SUPPORTED;
}
