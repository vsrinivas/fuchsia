// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// This is the same as memcpy, except that it takes the additional
// argument of &current_thread()->arch.data_fault_resume, where it
// temporarily stores the fault recovery PC for bad page faults to user
// addresses during the call.  arch_copy_from_user and arch_copy_to_user
// should be the only callers of this.
zx_status_t _arm64_user_copy(
    void* dst,
    const void* src,
    size_t len,
    void** fault_return);

__END_CDECLS
