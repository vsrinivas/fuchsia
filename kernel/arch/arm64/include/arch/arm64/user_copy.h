// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once
#include <zircon/compiler.h>

__BEGIN_CDECLS

/* These functions are identical to arch_copy_from_user, except they take an
 * additional argument of fault_return, used to handle page faults within the
 * function.  These should not be called anywhere except in the arm64 usercopy
 * implementation. */

status_t _arm64_copy_from_user(
        void *dst,
        const void *src,
        size_t len,
        void **fault_return);

status_t _arm64_copy_to_user(
        void *dst,
        const void *src,
        size_t len,
        void **fault_return);

__END_CDECLS
