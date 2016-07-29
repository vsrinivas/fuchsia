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

// for ssize_t
#include <unistd.h>

#include <magenta/types.h>
#include <system/compiler.h>

#define MAX_MXIO_FD 256

// flag on handle args in processargs
// instructing that this fd should be dup'd to 0/1/2
// and be used for all of stdio
#define MXIO_FLAG_USE_FOR_STDIO 0x8000

#define MXIO_NONBLOCKING 1

#define MXIO_PROTOCOL_UNDEFINED 0
#define MXIO_PROTOCOL_PIPE 1
#define MXIO_PROTOCOL_REMOTE 2

// maximum handles used in open/clone/create
#define MXIO_MAX_HANDLES 3

// mxio_ops_t's read/write are able to do io of
// at least this size
#define MXIO_CHUNK_SIZE 8192

// Maxium size for an ioctl input
#define MXIO_IOCTL_MAX_INPUT 1024

// events for mxio_wait_fd()
#define MXIO_EVT_READABLE MX_SIGNAL_USER0
#define MXIO_EVT_WRITABLE MX_SIGNAL_USER1
#define MXIO_EVT_ERROR MX_SIGNAL_USER2
#define MXIO_EVT_ALL (MXIO_EVT_READABLE | MXIO_EVT_WRITABLE | MXIO_EVT_ERROR)

__BEGIN_CDECLS

// wait until one or more events are pending
mx_status_t mxio_wait_fd(int fd, uint32_t events, uint32_t* pending, mx_time_t timeout);

// invoke a raw mxio ioctl
ssize_t mxio_ioctl(int fd, int op, const void* in_buf, size_t in_len, void* out_buf, size_t out_len);

// create a pipe, installing one half in a fd, returning the other
// for transport to another process
mx_status_t mxio_pipe_half(mx_handle_t* handle, uint32_t* type);

#define IOCTL_DEVICE_GET_HANDLE 0x7FFF0001

__END_CDECLS