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

#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <mxio/io.h>

#include "util.h"

typedef struct mx_pipe mx_pipe_t;
struct mx_pipe {
    mxio_t io;
    mx_handle_t h;
    uint32_t avail;
    uint32_t flags;
    uint8_t* next;
    uint8_t data[MXIO_CHUNK_SIZE];
};

static ssize_t mx_pipe_write(mxio_t* io, const void* _data, size_t len) {
    mx_pipe_t* p = (mx_pipe_t*)io;
    const uint8_t* data = _data;
    mx_status_t r = 0;
    ssize_t count = 0;

    while (len > 0) {
        size_t xfer = (len > MXIO_CHUNK_SIZE) ? MXIO_CHUNK_SIZE : len;
        r = _magenta_message_write(p->h, data, xfer, NULL, 0, 0);
        if (r < 0)
            break;
        len -= xfer;
        count += xfer;
        data += xfer;
    }

    // prioritize partial write results over errors
    return count ? count : r;
}

static ssize_t mx_pipe_read(mxio_t* io, void* _data, size_t len) {
    mx_pipe_t* p = (mx_pipe_t*)io;
    uint8_t* data = _data;
    mx_status_t r = 0;
    ssize_t count = 0;

    while (len > 0) {
        if (p->avail == 0) {
            if (len >= MXIO_CHUNK_SIZE) {
                // largest message will fit, read directly
                r = mxu_blocking_read(p->h, data, MXIO_CHUNK_SIZE);
                if (r <= 0)
                    break;
                data += r;
                len -= r;
                count += r;
                continue;
            } else {
                r = mxu_blocking_read(p->h, p->data, MXIO_CHUNK_SIZE);
                if (r <= 0)
                    break;
                p->avail = r;
                p->next = p->data;
            }
        }
        size_t xfer = (p->avail > len) ? len : p->avail;
        memcpy(data, p->next, xfer);
        p->next += xfer;
        p->avail -= xfer;
        data += xfer;
        len -= xfer;
        count += xfer;
    }

    // prioritize partial read results over errors
    return count ? count : r;
}

static mx_status_t mx_pipe_close(mxio_t* io) {
    mx_pipe_t* p = (mx_pipe_t*)io;
    _magenta_handle_close(p->h);
    free(p);
    return 0;
}

static mx_status_t mx_pipe_misc(mxio_t* io, uint32_t op, uint32_t maxreply, void* ptr, size_t len) {
    return ERR_NOT_SUPPORTED;
}

static mx_status_t mx_pipe_open(mxio_t* io, const char* path, int32_t flags, mxio_t** out) {
    return ERR_NOT_SUPPORTED;
}

static mx_handle_t mx_pipe_clone(mxio_t* io, mx_handle_t* handles, uint32_t* types) {
    return ERR_NOT_SUPPORTED;
}

static off_t mx_pipe_seek(mxio_t* io, off_t offset, int whence) {
    return ERR_NOT_SUPPORTED;
}

static mx_status_t mx_pipe_wait(mxio_t* io, uint32_t events, uint32_t* pending, mx_time_t timeout) {
    return ERR_NOT_SUPPORTED;
}

static mxio_ops_t mx_pipe_ops = {
    .read = mx_pipe_read,
    .write = mx_pipe_write,
    .misc = mx_pipe_misc,
    .close = mx_pipe_close,
    .open = mx_pipe_open,
    .clone = mx_pipe_clone,
    .seek = mx_pipe_seek,
    .wait = mx_pipe_wait,
};

mxio_t* mxio_pipe_create(mx_handle_t h) {
    mx_pipe_t* p = malloc(sizeof(*p));
    if (p == NULL)
        return NULL;
    p->io.ops = &mx_pipe_ops;
    p->io.magic = MXIO_MAGIC;
    p->io.priv = 0;
    p->h = h;
    p->avail = 0;
    p->flags = 0;
    return &p->io;
}

int mxio_pipe_pair(mxio_t** _a, mxio_t** _b) {
    mx_handle_t ha, hb;
    mxio_t *a, *b;
    ha = _magenta_message_pipe_create(&hb);
    if (ha < 0)
        return ha;
    if ((a = mxio_pipe_create(ha)) == NULL) {
        _magenta_handle_close(ha);
        _magenta_handle_close(hb);
        return ERR_NO_MEMORY;
    }
    if ((b = mxio_pipe_create(hb)) == NULL) {
        mx_pipe_close(a);
        _magenta_handle_close(hb);
        return ERR_NO_MEMORY;
    }
    *_a = a;
    *_b = b;
    return 0;
}

mx_status_t mxio_pipe_pair_raw(mx_handle_t* handles, uint32_t* types) {
    mx_handle_t ha, hb;
    if ((ha = _magenta_message_pipe_create(&hb)) < 0) {
        return ha;
    }
    handles[0] = ha;
    handles[1] = hb;
    types[0] = MX_HND_TYPE_MXIO_PIPE;
    types[1] = MX_HND_TYPE_MXIO_PIPE;
    return 2;
}
