// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_PROCESSARGS_PROCESSARGS_H_
#define LIB_PROCESSARGS_PROCESSARGS_H_

#include <stdalign.h>
#include <zircon/compiler.h>
#include <zircon/processargs.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Examine the next message to be read from the channel, and yield
// the data size and number of handles in that message.
zx_status_t processargs_message_size(zx_handle_t channel, uint32_t* nbytes, uint32_t* nhandles);

// Define a properly-aligned buffer on the stack for reading a
// processargs message.  The nbytes parameter should be gotten from
// processargs_message_size.
#define PROCESSARGS_BUFFER(variable, nbytes) alignas(zx_proc_args_t) uint8_t variable[nbytes]

// The buffer provided must be properly aligned (alignas(zx_proc_args_t))
// and large enough for the message pending on the given bootstrap
// message-pipe handle.  This reads the message into that buffer, validates
// the message format of, and yields pointers into the buffer for the
// header and the handle-info array.
zx_status_t processargs_read(zx_handle_t bootstrap, void* buffer, uint32_t nbytes,
                             zx_handle_t handles[], uint32_t nhandles, zx_proc_args_t** pargs,
                             uint32_t** handle_info);

// Extract known handle types from the handles.
// Extracted handles are reset (ZX_HANDLE_INVALID into handles and 0 into
// handle_info)
void processargs_extract_handles(uint32_t nhandles, zx_handle_t handles[], uint32_t handle_info[],
                                 zx_handle_t* process_self, zx_handle_t* job_default,
                                 zx_handle_t* vmar_root_self, zx_handle_t* thread_self,
                                 zx_handle_t* utc_reference);

// This assumes processargs_read has already succeeded on the same
// buffer.  It unpacks the argument and environment strings into arrays
// provided by the caller.  If not NULL, the argv[] array must have
// zx_proc_args_t.args_num + 1 elements.  If not NULL, the envp[] array
// must have zx_proc_args_t.environ_num + 1 elements.  If not NULL, the
// names[] array must have zx_proc_args_t.names_num + 1 elements. The
// last element of each array is filled with a NULL pointer.
zx_status_t processargs_strings(void* msg, uint32_t bytes, char* argv[], char* envp[],
                                char* names[]);

__END_CDECLS

#endif  // LIB_PROCESSARGS_PROCESSARGS_H_
