// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_LOGGER_H_
#define LIB_FTL_LOGGER_H_

#include <zircon/compiler.h>

__BEGIN_CDECLS

// Helper for overriding default logging routines.
typedef struct FtlLogger {
  __PRINTFLIKE(3, 4) void (*trace)(const char*, int, const char*, ...);
  __PRINTFLIKE(3, 4) void (*debug)(const char*, int, const char*, ...);
  __PRINTFLIKE(3, 4) void (*info)(const char*, int, const char*, ...);
  __PRINTFLIKE(3, 4) void (*warn)(const char*, int, const char*, ...);
  __PRINTFLIKE(3, 4) void (*error)(const char*, int, const char*, ...);
} FtlLogger;

__END_CDECLS

#endif  // LIB_FTL_LOGGER_H_
