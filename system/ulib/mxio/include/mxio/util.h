// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <magenta/types.h>
#include <system/compiler.h>
#include <mxio/io.h>
#include <stdint.h>

__BEGIN_CDECLS

// These routines are "internal" to mxio but used by some companion
// code like userboot and devmgr

typedef struct mxio mxio_t;

// starts new process, handling fd/handle transfer
mx_handle_t mxio_start_process(const char* name, int argc, char** argv);

// Starts new process, manual configuration of initial handle set.
// Handles and ids must be one larger than hnds_count as the
// process handle is added at the very end.
mx_handle_t mxio_start_process_etc(const char* name, int args_count, char* args[],
                                   int hnds_count, mx_handle_t* handles, uint32_t* ids);

// Utilities to help assemble handles for a new process
// may return up to MXIO_MAX_HANDLES
mx_status_t mxio_clone_root(mx_handle_t* handles, uint32_t* types);
mx_status_t mxio_clone_fd(int fd, int newfd, mx_handle_t* handles, uint32_t* types);
mx_status_t mxio_pipe_pair_raw(mx_handle_t* handles, uint32_t* types);

// Create a handle containing process arguments.
// If proc is nonzero, it will be added to the
// end of the handle/id tables, so they must
// be one larger than specified in hnds_count.
mx_handle_t mxio_build_procargs(int args_count, char* args[],
                                int auxv_count, uintptr_t auxv[],
                                int hnds_count, mx_handle_t* handles,
                                uint32_t* ids, mx_handle_t proc);

// Load a static elf binary into a process from memory buffer or fd
mx_status_t mxio_load_elf_mem(mx_handle_t process, mx_vaddr_t* entry, void* data, size_t len);
mx_status_t mxio_load_elf_fd(mx_handle_t process, mx_vaddr_t* entry, int fd);

mx_status_t mxio_load_elf_filename(mx_handle_t process, const char* filename,
                                   int* auxv_count, uintptr_t auxv[],
                                   mx_vaddr_t* entry);

void bootfs_parse(void* _data, int len,
                  void (*cb)(const char* fn, size_t off, size_t len));


// used for bootstrap
void mxio_install_root(mxio_t* root);

// attempt to install a mxio in the unistd fd table
// if fd >= 0, request a specific fd, and starting_fd is ignored
// if fd < 0, request the first available fd >= starting_fd
// returns fd on success
// the mxio must have been upref'd on behalf of the fdtab first
int mxio_bind_to_fd(mxio_t* io, int fd, int starting_fd);

// creates a do-nothing mxio_t
mxio_t* mxio_null_create(void);

// wraps a message port with an mxio_t using remote io
mxio_t* mxio_remote_create(mx_handle_t h, mx_handle_t e);

// creates a mxio that wraps a log object
// this will allocate a per-thread buffer (on demand) to assemble
// entire log-lines and flush them on newline or buffer full.
mxio_t* mxio_logger_create(mx_handle_t);

// Start a thread to resolve loader service requests
// and return a message pipe handle to talk to said service
mx_handle_t mxio_loader_service(void);

__END_CDECLS
