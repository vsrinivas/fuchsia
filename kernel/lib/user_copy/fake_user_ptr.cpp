// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/user_copy/fake_user_ptr.h>

#include <string.h>

namespace internal {

namespace testing {

zx_status_t unsafe_copy(void* dst, const void* src, size_t len) {
    memcpy(dst, src, len);
    return ZX_OK;
}

}  // namespace testing

}  // namespace internal
