// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_NIX_INCLUDE_SYS_RANDOM_H_
#define SRC_LIB_NIX_INCLUDE_SYS_RANDOM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <unistd.h>

#define GRND_NONBLOCK 0x0001
#define GRND_RANDOM 0x0002

// Fills the provided |buffer| with |buffer_size| random bytes. The |flags|
// argument is a bitmask that can contain zero or more of the flags
// GRND_RANDOM or GRND_NONBLOCK ORed together.
//
// On success, returns the number of bytes written, which will always be
// |buffer_size|. On error, -1 is returned, and errno is set appropriately.
ssize_t getrandom(void* buffer, size_t buffer_size, unsigned int flags);

#ifdef __cplusplus
}
#endif

#endif  // SRC_LIB_NIX_INCLUDE_SYS_RANDOM_H_
