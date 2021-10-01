// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_TYPES_H_
#define LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_TYPES_H_

#include <zircon/syscalls.h>

__BEGIN_CDECLS

// TODO(fxbug.dev/85595): use our own error types
typedef zx_status_t fdf_status_t;

__END_CDECLS

#endif  // LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_TYPES_H_
