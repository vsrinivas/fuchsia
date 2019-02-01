// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/random/rand.h"

#include "lib/fxl/build_config.h"
#include "lib/fxl/logging.h"

#if defined(OS_FUCHSIA)
#include <zircon/syscalls.h>

#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "lib/fxl/files/file_descriptor.h"
#include "lib/fxl/files/unique_fd.h"
#endif

namespace fxl {

void RandBytes(void* output, size_t output_length) {
  FXL_DCHECK(output);

#if defined(OS_FUCHSIA)
  zx_cprng_draw(output, output_length);
  return;
#else
  fxl::UniqueFD fd(open("/dev/urandom", O_RDONLY | O_CLOEXEC));
  FXL_CHECK(fd.is_valid());
  const ssize_t len =
      ReadFileDescriptor(fd.get(), static_cast<char*>(output), output_length);
  FXL_CHECK(len >= 0 && static_cast<size_t>(len) == output_length);
#endif
}

uint64_t RandUint64() {
  uint64_t number;
  RandBytes(&number, sizeof(number));
  return number;
}

}  // namespace fxl
