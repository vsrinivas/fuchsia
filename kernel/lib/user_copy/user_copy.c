// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <compiler.h>
#include <stdlib.h>
#include <string.h>
#include <arch/user_copy.h>
#include <lib/user_copy.h>

status_t copy_from_user_dynamic(
        void **dst,
        const void *src,
        size_t len,
        size_t max_len)
{
    DEBUG_ASSERT(dst);
    if (len > max_len) {
        return ERR_INVALID_ARGS;
    }
    void *buf = calloc(len, 1);
    if (!buf) {
        return ERR_NO_MEMORY;
    }
    status_t status = copy_from_user(buf, src, len);
    if (status != NO_ERROR) {
        free(buf);
        return status;
    }
    *dst = buf;
    return NO_ERROR;
}

// Default implementations of arch_copy* functions, almost certainly
// want to override with arch-specific versions that check access permissions
__WEAK status_t arch_copy_from_user(void *dst, const void *src, size_t len) {
    memcpy(dst, src, len);
    return NO_ERROR;
}

__WEAK status_t arch_copy_to_user(void *dst, const void *src, size_t len) {
    memcpy(dst, src, len);
    return NO_ERROR;
}
