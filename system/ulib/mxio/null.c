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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <magenta/types.h>
#include <mxio/io.h>

static ssize_t mx_null_write(mxio_t* io, const void* _data, size_t len) {
    return len;
}

static ssize_t mx_null_read(mxio_t* io, void* _data, size_t len) {
    return 0;
}

static mx_status_t mx_null_close(mxio_t* io) {
    return 0;
}

static mx_status_t mx_null_misc(mxio_t* io, uint32_t op, uint32_t arg, void* data, size_t len) {
    return ERR_NOT_SUPPORTED;
}

static mx_status_t mx_null_open(mxio_t* io, const char* path, int32_t flags, mxio_t** out) {
    return ERR_NOT_SUPPORTED;
}

static mx_handle_t mx_null_clone(mxio_t* io, mx_handle_t* handles, uint32_t* types) {
    return ERR_NOT_SUPPORTED;
}

static off_t mx_null_seek(mxio_t* io, off_t offset, int whence) {
    return NO_ERROR;
}

static mx_status_t mx_null_wait(mxio_t* io, uint32_t events, uint32_t *pending, mx_time_t timeout) {
    return ERR_NOT_SUPPORTED;
}

static mxio_ops_t mx_null_ops = {
    .read = mx_null_read,
    .write = mx_null_write,
    .misc = mx_null_misc,
    .close = mx_null_close,
    .open = mx_null_open,
    .clone = mx_null_clone,
    .seek = mx_null_seek,
    .wait = mx_null_wait,
};

mxio_t* mxio_null_create(void) {
    mxio_t* io = malloc(sizeof(*io));
    if (io == NULL) return NULL;
    io->ops = &mx_null_ops;
    io->magic = MXIO_MAGIC;
    io->priv = 0;
    return io;
}
