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

#include "private.h"

#include <stddef.h>
#include <stdlib.h>

#include <magenta/types.h>

typedef struct mx_socket {
    mxio_t io;
    mx_handle_t h;
} mx_socket_t;

static mxio_ops_t mx_socket_ops = {
    .read = mxio_default_read,
    .write = mxio_default_write,
    .seek = mxio_default_seek,
    .misc = mxio_default_misc,
    .close = mxio_default_close,
    .open = mxio_default_open,
    .clone = mxio_default_clone,
    .wait = mxio_default_wait,
    .ioctl = mxio_default_ioctl,
};

mxio_t* mxio_socket_create(mx_handle_t h) {
    mx_socket_t* p = calloc(1, sizeof(*p));
    if (p == NULL)
        return NULL;
    p->io.ops = &mx_socket_ops;
    p->io.magic = MXIO_MAGIC;
    p->io.refcount = 1;
    p->io.flags |= MXIO_FLAG_SOCKET;
    p->h = h;
    return &p->io;
}
