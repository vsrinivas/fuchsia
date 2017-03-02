// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <limits.h>

// Maximum number of fds per process.
#define MAX_MXIO_FD 256

// Maximum handles used in open/clone/create.
#define MXIO_MAX_HANDLES 3

// mxio_ops_t's read/write are able to do io of
// at least this size.
#define MXIO_CHUNK_SIZE 8192

// Maximum size for an ioctl input.
#define MXIO_IOCTL_MAX_INPUT 1024

// Maxium length of a filename.
#define MXIO_MAX_FILENAME NAME_MAX
