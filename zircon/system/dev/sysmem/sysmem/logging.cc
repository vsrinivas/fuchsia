// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logging.h"

#include <stdio.h>
#include <zircon/assert.h>

#include <memory>

#include <ddk/debug.h>

namespace sysmem_driver {

void vLog(bool is_error, const char* prefix1, const char* prefix2, const char* format,
          va_list args) {
  // Let's not have a buffer on the stack, not because it couldn't be done
  // safely, but because we'd potentially run into stack size vs. message
  // length tradeoffs, stack expansion granularity fun, or whatever else.

  va_list args2;
  va_copy(args2, args);

  size_t buffer_bytes = vsnprintf(nullptr, 0, format, args) + 1;

  std::unique_ptr<char[]> buffer(new char[buffer_bytes]);

  size_t buffer_bytes_2 = vsnprintf(buffer.get(), buffer_bytes, format, args2) + 1;
  (void)buffer_bytes_2;
  // sanity check; should match so go ahead and assert that it does.
  ZX_DEBUG_ASSERT(buffer_bytes == buffer_bytes_2);
  va_end(args2);

  if (is_error) {
    zxlogf(ERROR, "[%s %s] %s\n", prefix1, prefix2, buffer.get());
  } else {
    zxlogf(TRACE, "[%s %s] %s\n", prefix1, prefix2, buffer.get());
  }
}

}  // namespace sysmem_driver
