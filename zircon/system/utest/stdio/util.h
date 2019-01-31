// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// readable: is the pipe readable on the child side?
// returns [our_fd, child_fd]
int stdio_pipe(int pipe_fds[2], bool readable);

int read_to_end(int fd, uint8_t** buf, size_t* buf_size);
