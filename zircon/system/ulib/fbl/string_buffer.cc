// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/string_buffer.h>

#include <stdio.h>
#include <string.h>

namespace fbl::internal {

size_t StringBufferAppendPrintf(char* dest, size_t remaining,
                                const char* format, va_list ap) {
    if (remaining == 0U) {
        return 0U;
    }
    int count = vsnprintf(dest, remaining + 1U, format, ap);
    if (count < 0) {
        return 0U;
    }
    size_t length = static_cast<size_t>(count);
    return length >= remaining ? remaining : length;
}

} // namespace fbl::internal
