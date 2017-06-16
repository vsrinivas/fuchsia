// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <magenta/compiler.h>
#include <stdlib.h>
#include <string.h>
#include <arch/user_copy.h>
#include <lib/user_copy.h>

// Default implementations of arch_copy* functions, almost certainly
// want to override with arch-specific versions that check access permissions
__WEAK status_t arch_copy_from_user(void *dst, const void *src, size_t len) {
    memcpy(dst, src, len);
    return MX_OK;
}

__WEAK status_t arch_copy_to_user(void *dst, const void *src, size_t len) {
    memcpy(dst, src, len);
    return MX_OK;
}
