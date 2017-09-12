// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/processargs.h>
#include <zircon/types.h>
#include <stdalign.h>

__BEGIN_CDECLS

#pragma GCC visibility push(hidden)

// Define a properly-aligned buffer on the stack for reading a processargs
// message.  The nbytes parameter should be gotten from zxr_message_size.
#define ZXR_PROCESSARGS_BUFFER(variable, nbytes) \
    alignas(zx_proc_args_t) uint8_t variable[nbytes]

// The buffer provided must be properly aligned (alignas(zx_proc_args_t))
// and large enough for the message pending on the given bootstrap
// message-pipe handle.  This reads the message into that buffer, validates
// the message format of, and yields pointers into the buffer for the
// header and the handle-info array.
zx_status_t zxr_processargs_read(zx_handle_t bootstrap,
                                 void* buffer, uint32_t nbytes,
                                 zx_handle_t handles[], uint32_t nhandles,
                                 zx_proc_args_t** pargs,
                                 uint32_t** handle_info);

// This assumes zxr_processargs_read has already succeeded on the same
// buffer.  It unpacks the argument and environment strings into arrays
// provided by the caller.  If not NULL, the argv[] array must have
// zx_proc_args_t.args_num + 1 elements.  If not NULL, the envp[] array
// must have zx_proc_args_t.environ_num + 1 elements.  If not NULL, the
// names[] array must have zx_proc_args_t.names_num + 1 elements. The
// last element of each array is filled with a NULL pointer.
zx_status_t zxr_processargs_strings(void* msg, uint32_t bytes,
                                    char* argv[], char* envp[],
                                    char* names[]);

#pragma GCC visibility pop

__END_CDECLS
