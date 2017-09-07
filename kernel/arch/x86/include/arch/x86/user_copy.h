// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once
#include <magenta/compiler.h>

__BEGIN_CDECLS

/* This function is used by arch_copy_from_user() and arch_copy_to_user().
 * It should not be called anywhere except in the x86 usercopy
 * implementation. */

status_t _x86_copy_to_or_from_user(
        void *dst,
        const void *src,
        size_t len,
        void **fault_return);

__END_CDECLS
