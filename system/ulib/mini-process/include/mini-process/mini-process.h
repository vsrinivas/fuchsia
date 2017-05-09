// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <magenta/compiler.h>

__BEGIN_CDECLS

// Create and run a minimal process with one thread that blocks forever.
// Does not require a host binary.
mx_status_t start_mini_process(mx_handle_t job, mx_handle_t transfered_handle,
                               mx_handle_t* process, mx_handle_t* thread);

// Like start_mini_process() but requires caller to create the process,
// thread and object to transfer.  If the busy_wait_no_vdso flag is set,
// the mini-process runs a busy loop rather than a blocking syscall, and
// does not have a vDSO mapped in.
mx_status_t start_mini_process_etc(mx_handle_t process, mx_handle_t thread,
                                   mx_handle_t vmar,
                                   mx_handle_t transfered_handle,
                                   bool busy_wait_no_vdso);

__END_CDECLS
