// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

extern "C" __EXPORT __NO_RETURN __PRINTFLIKE(1, 2) void __zx_panic(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);

  // The format string is not required to have \n on the end in order to avoid a run-on line.  While
  // some implementations below don't need this \n for the output to look correct, some do, so we
  // add the \n here.
  fprintf(stderr, "\n");

  // This fflush(nullptr) will ensure that any data written to stderr above is flushed out of this
  // process (in addition to the usual flushing of stdout and other FILE(s)).  This includes
  // flushing out of this process any data buffered below writev() layer (canonically the syscall
  // layer) but above the process boundary (where buffering can exist since writev() isn't actually
  // the syscall layer).  See also __fflush_unlocked() calling f->write(f, 0, 0), and
  // Debuglog::Writev() noticing the vector entry with buffer nullptr and capacity 0, which will
  // ensure the vfprintf(stderr, ...) above is output (without relying on the \n above, fwiw).
  fflush(nullptr);

  // This typically will trigger a stack crawl (performed by a different process).
  abort();
}
