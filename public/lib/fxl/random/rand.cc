// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/random/rand.h"

#include "lib/fxl/build_config.h"
#include "lib/fxl/logging.h"

#if defined(OS_FUCHSIA)
#include <zircon/syscalls.h>

#elif defined(OS_WIN)
#include <windows.h>
#include <stddef.h>
#include <stdint.h>
// #define needed to link in RtlGenRandom(), a.k.a. SystemFunction036.  See the
// "Community Additions" comment on MSDN here:
// http://msdn.microsoft.com/en-us/library/windows/desktop/aa387694.aspx
#define SystemFunction036 NTAPI SystemFunction036
#include <NTSecAPI.h>
#undef SystemFunction036
#include <algorithm>
#include <limits>

#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "lib/fxl/files/file_descriptor.h"
#include "lib/fxl/files/unique_fd.h"
#endif

namespace fxl {

uint64_t RandUint64() {
  uint64_t number;
  bool success =
    RandBytes(reinterpret_cast<unsigned char*>(&number), sizeof(number));

  FXL_CHECK(success);
  return number;
}

bool RandBytes(unsigned char* output, size_t output_length) {
  FXL_DCHECK(output);

#if defined(OS_FUCHSIA)
  size_t remaining = output_length;
  unsigned char* offset = output;
  size_t actual;
  size_t read_len;
  do {
    // We can only read a limited number of bytes via the syscall at a time.
    read_len =
        remaining > ZX_CPRNG_DRAW_MAX_LEN ? ZX_CPRNG_DRAW_MAX_LEN : remaining;

    zx_status_t status = zx_cprng_draw(offset, read_len, &actual);
    FXL_CHECK(status == ZX_OK);

    // decrement the remainder and update the pointer offset in the buffer
    remaining -= actual;
    offset += actual;

  } while (remaining > 0);

  return true;
#elif defined(OS_WIN)
  bool success = true;
  while (output_length > 0) {
    const ULONG output_bytes_this_pass = std::min(
    static_cast<ULONG>(output_length), std::numeric_limits<ULONG>::max());
    success =
        RtlGenRandom(output, output_bytes_this_pass) != FALSE;
    if (!success)
      return false;
    output_length -= output_bytes_this_pass;
    output += output_bytes_this_pass;
  }
  return success;
#else
  fxl::UniqueFD fd(open("/dev/urandom", O_RDONLY | O_CLOEXEC));
  if (!fd.is_valid())
    return false;

  const bool success =
    ReadFileDescriptor(fd.get(), reinterpret_cast<char*>(output), output_length);

  FXL_DCHECK(success);
  return success;
#endif
}

}  // namespace fxl
