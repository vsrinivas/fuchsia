// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/random/rand.h"

#include "lib/fxl/build_config.h"
#include "lib/fxl/logging.h"

#if defined(OS_FUCHSIA)
#include <zircon/syscalls.h>

#elif defined(OS_WIN)
#include <stddef.h>
#include <stdint.h>
#include <windows.h>
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
  bool success = RandBytes(&number, sizeof(number));

  FXL_CHECK(success);
  return number;
}

bool RandBytes(void* output, size_t output_length) {
  FXL_DCHECK(output);

#if defined(OS_FUCHSIA)
  zx_cprng_draw(output, output_length);
  return true;
#elif defined(OS_WIN)
  unsigned char* offset = static_cast<unsigned char*>(output);
  bool success = true;
  while (output_length > 0) {
    const ULONG output_bytes_this_pass = std::min(
        static_cast<ULONG>(output_length), std::numeric_limits<ULONG>::max());
    success = RtlGenRandom(offset, output_bytes_this_pass) != FALSE;
    if (!success)
      return false;
    output_length -= output_bytes_this_pass;
    offset += output_bytes_this_pass;
  }
  return success;
#else
  fxl::UniqueFD fd(open("/dev/urandom", O_RDONLY | O_CLOEXEC));
  if (!fd.is_valid())
    return false;

  const bool success =
      ReadFileDescriptor(fd.get(), static_cast<char*>(output), output_length);

  FXL_DCHECK(success);
  return success;
#endif
}

}  // namespace fxl
