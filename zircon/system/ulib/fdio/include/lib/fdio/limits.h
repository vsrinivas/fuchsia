// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <limits.h>

// Maximum number of fds per process.
#define FDIO_MAX_FD 256

// Maximum handles used in open/clone/create.
#define FDIO_MAX_HANDLES 3

// fdio_ops_t's read/write are able to do io of
// at least this size.
#define FDIO_CHUNK_SIZE 8192

// Maximum size for an ioctl input.
#define FDIO_IOCTL_MAX_INPUT 1024

// Maximum length of a filename.
#define FDIO_MAX_FILENAME NAME_MAX
