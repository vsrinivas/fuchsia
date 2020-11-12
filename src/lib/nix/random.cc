// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <sys/random.h>
#include <zircon/syscalls.h>

ssize_t getrandom(void* buffer, size_t buffer_size, unsigned int flags) {
  // The |flags| argument is only validated for valid bits and is otherwise
  // ignored. The implementation uses zx_cprng_draw, which does not block
  // (GRND_NONBLOCK is not required), and supplies random data suited for
  // cryptographic operations (GRND_RANDOM is not required).
  if (flags & ~(GRND_NONBLOCK | GRND_RANDOM)) {
    errno = EINVAL;
    return -1;
  }
  zx_cprng_draw(buffer, buffer_size);
  return buffer_size;
}
