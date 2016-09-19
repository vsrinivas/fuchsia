// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/processargs.h>
#include <magenta/types.h>
#include <stdalign.h>

__BEGIN_CDECLS

#pragma GCC visibility push(hidden)

// Define a properly-aligned buffer on the stack for reading a processargs
// message.  The nbytes parameter should be gotten from mxr_message_size.
#define MXR_PROCESSARGS_BUFFER(variable, nbytes) \
    alignas(mx_proc_args_t) uint8_t variable[nbytes]

// The buffer provided must be properly aligned (alignas(mx_proc_args_t))
// and large enough for the message pending on the given bootstrap
// message-pipe handle.  This reads the message into that buffer, validates
// the message format of, and yields pointers into the buffer for the
// header and the handle-info array.
mx_status_t mxr_processargs_read(mx_handle_t bootstrap,
                                 void* buffer, uint32_t nbytes,
                                 mx_handle_t handles[], uint32_t nhandles,
                                 mx_proc_args_t** pargs,
                                 uint32_t** handle_info);

// This assumes mxr_processargs_read has already succeeded on the same
// buffer.  It unpacks the argument and environment strings into arrays
// provided by the caller.  If not NULL, the argv[] array must have
// mx_proc_args_t.args_num + 1 elements.  If not NULL, the envp[] array
// must have mx_proc_args_t.environ_num + 1 elements.  The last element
// of each array is filled with a NULL pointer.
mx_status_t mxr_processargs_strings(void* msg, uint32_t bytes,
                                    char* argv[], char* envp[]);

#pragma GCC visibility pop

__END_CDECLS
