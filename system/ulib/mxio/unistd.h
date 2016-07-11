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

#include <mxio/io.h>
#include <limits.h>

#include "private.h"

typedef struct {
    mxr_mutex_t lock;
    mxr_mutex_t cwd_lock;
    bool init;
    mode_t umask;
    mxio_t* root;
    mxio_t* cwd;
    mxio_t* fdtab[MAX_MXIO_FD];
    char cwd_path[PATH_MAX];
} mxio_state_t;

extern mxio_state_t __mxio_global_state;

#define mxio_lock (__mxio_global_state.lock)
#define mxio_root_handle (__mxio_global_state.root)
#define mxio_cwd_handle (__mxio_global_state.cwd)
#define mxio_cwd_lock (__mxio_global_state.cwd_lock)
#define mxio_cwd_path (__mxio_global_state.cwd_path)
#define mxio_fdtab (__mxio_global_state.fdtab)
#define mxio_root_init (__mxio_global_state.init)

mxio_t* __mxio_fd_to_io(int fd);

#define fd_to_io(n) __mxio_fd_to_io(n)