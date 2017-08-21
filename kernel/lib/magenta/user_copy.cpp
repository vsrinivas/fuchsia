// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/user_copy.h>

#include <stdint.h>

#include <lib/user_copy.h>

mx_status_t magenta_copy_from_user(const void* src, void* dest, size_t len) {
    if (src == nullptr) return MX_ERR_INVALID_ARGS;
    mx_status_t status = copy_from_user_unsafe(dest, src, len);
    if (status != MX_OK) {
        return MX_ERR_INVALID_ARGS;
    }
    return MX_OK;
}

mx_status_t magenta_copy_user_string(const char* src, size_t src_len, char* buf, size_t buf_len,
                                     mxtl::StringPiece* sp) {
    if (src_len > buf_len) return MX_ERR_INVALID_ARGS;

    mx_status_t result = magenta_copy_from_user(src, buf, src_len);
    if (result != MX_OK) return result;

    // ensure zero termination
    size_t str_len = (src_len == buf_len ? src_len - 1 : src_len);
    buf[str_len] = 0;
    *sp = mxtl::StringPiece(buf);

    return MX_OK;
}
