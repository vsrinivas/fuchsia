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
#include <mxio/io.h>
#include <sys/types.h>

// These routines are "internal" to mxio but used by some companion
// code like userboot and devmgr

// starts new process, handling fd/handle transfer
mx_handle_t mxio_start_process(int argc, char** argv);

// Starts new process, manual configuration of initial handle set.
// Handles and ids must be one larger than hnds_count as the
// process handle is added at the very end.
mx_handle_t mxio_start_process_etc(int args_count, char* args[],
                                   int hnds_count, mx_handle_t* handles, uint32_t* ids);

// Utilities to help assemble handles for a new process
// may return up to MXIO_MAX_HANDLES
mx_status_t mxio_clone_root(mx_handle_t* handles, uint32_t* types);
mx_status_t mxio_clone_fd(int fd, int newfd, mx_handle_t* handles, uint32_t* types);
mx_status_t mxio_pipe_pair_raw(mx_handle_t* handles, uint32_t* types);

// Interface for calling into our temporary ioctl
ssize_t mxio_ioctl(int fd, int op, const void* in_buf, size_t in_len, void* out_buf, size_t out_len);

// Create a handle containing process arguments.
// If proc is nonzero, it will be added to the
// end of the handle/id tables, so they must
// be one larger than specified in hnds_count.
mx_handle_t mxio_build_procargs(int args_count, char* args[], int hnds_count,
                                mx_handle_t* handles, uint32_t* ids, mx_handle_t proc);

mx_status_t mxio_load_elf_mem(mx_handle_t process, mx_vaddr_t* entry, void* data, size_t len);

mx_status_t mxio_load_elf_fd(mx_handle_t process, mx_vaddr_t* entry, int fd);

// call from libc glue
void mxio_init(void* arg, int* argc, char*** argv);

void bootfs_parse(void* _data, int len,
                  void (*cb)(const char* fn, size_t off, size_t len));

// return our own process handle if we know it
mx_handle_t mxio_get_process_handle(void);

// used for bootstrap
void mxio_install_root(mxio_t* root);
