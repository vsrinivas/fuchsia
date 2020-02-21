// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "util.h"

#include <zircon/assert.h>
#include <zircon/compiler.h>

extern "C" __NO_RETURN __PRINTFLIKE(1, 2) void __zx_panic(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  // This uses zx_debug_write().  The log handle isn't plumbed to __zx_panic.
  vprintl(ZX_HANDLE_INVALID, fmt, ap);
  va_end(ap);
  __builtin_trap();
}
