// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>

#include <unittest/unittest.h>

// readable: is the pipe readable on the child side?
// returns [our_fd, child_fd]
int stdio_pipe(int pipe_fds[2], bool readable) {
    int r;
    if ((r = pipe(pipe_fds)) != 0) { // Initially gives [reader, writer]
        return r;
    }

    if (readable) {
        // If child is to be readable, we want
        // [our_fd: writer, child_fd: reader], so we must swap
        int tmp = pipe_fds[0];
        pipe_fds[0] = pipe_fds[1];
        pipe_fds[1] = tmp;
    }

    return 0;
}

int read_to_end(int fd, uint8_t** buf, size_t* buf_size) {
    size_t start_len = *buf_size;
    size_t unused = 16;

    *buf_size += unused;
    *buf = realloc(*buf, *buf_size);

    while (1) {
        if (unused == 0) {
            // Double the buffer size
            unused = *buf_size;
            *buf_size += unused;
            *buf = realloc(*buf, *buf_size);
        }

        uint8_t* buf_slice = &(*buf)[*buf_size-unused];
        int result = read(fd, buf_slice, unused);
        if (result == 0) {
            *buf_size -= unused;
            return *buf_size - start_len;
        } else if (result > 0) {
            unused -= result;
        } else if (result == EINTR) {
        } else {
            *buf_size -= unused;
            return result;
        }
    }
}
