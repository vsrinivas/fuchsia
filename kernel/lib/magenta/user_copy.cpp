// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <new.h>
#include <stdint.h>

#include <lib/user_copy.h>
#include <magenta/user_copy.h>

status_t magenta_copy_from_user(const void* src, void* dest, size_t len) {
    if (src == nullptr) return ERR_INVALID_ARGS;
    status_t status = copy_from_user_unsafe(dest, src, len);
    if (status != NO_ERROR) {
        return ERR_INVALID_ARGS;
    }
    return NO_ERROR;
}

status_t magenta_copy_user_string(const char* src, size_t src_len, char* buf, size_t buf_len,
                                  mxtl::StringPiece* sp) {
    if (src_len > buf_len) return ERR_INVALID_ARGS;

    status_t result = magenta_copy_from_user(src, buf, src_len);
    if (result != NO_ERROR) return result;

    // ensure zero termination
    size_t str_len = (src_len == buf_len ? src_len - 1 : src_len);
    buf[str_len] = 0;
    *sp = mxtl::StringPiece(buf);

    return NO_ERROR;
}

status_t magenta_copy_user_dynamic(const void* src, void** dest, size_t len, size_t max_len) {
    *dest = nullptr;

    if (len > max_len) return ERR_INVALID_ARGS;

    AllocChecker ac;
    auto buf = new (&ac) uint8_t[len];
    if (!ac.check()) return ERR_NO_MEMORY;

    status_t status = copy_from_user_unsafe(buf, src, len);
    if (status != NO_ERROR) {
        delete[] buf;
        return ERR_INVALID_ARGS;
    }
    *dest = buf;
    return NO_ERROR;
}
