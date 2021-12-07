/*
 *  Copyright (c) 2016, The OpenThread Authors.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the copyright holder nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

#include <lib/syslog/cpp/macros.h>

#include <openthread/platform/logging.h>
#include <openthread/platform/toolchain.h>

void otPlatLog(otLogLevel log_level, otLogRegion log_region, const char *format, ...) {
  OT_UNUSED_VARIABLE(log_region);

  va_list args;
  va_start(args, format);

  // Use vsnprintf to get size of buffer required to print formatted string
  // (excluding null terminator)
  size_t buffer_bytes = vsnprintf(nullptr, 0, format, args);
  FX_CHECK(buffer_bytes >= 0) << "Error in vsnprintf: " << errno;

  constexpr size_t null_terminator_bytes = 1;
  buffer_bytes += null_terminator_bytes;

  std::unique_ptr<char[]> buffer(new char[buffer_bytes]);

  // Now create the string which we want to send to log
  size_t buffer_bytes_2 = vsnprintf(buffer.get(), buffer_bytes, format, args) + 1;
  // sanity check; should match so go ahead and assert that it does.
  FX_CHECK(buffer_bytes == buffer_bytes_2)
      << "Error in vsnprintf or size not equal: " << buffer_bytes << " != " << buffer_bytes_2
      << ", errno = " << errno;
  va_end(args);

  otPlatLogLine(log_level, log_region, buffer.get());
}
