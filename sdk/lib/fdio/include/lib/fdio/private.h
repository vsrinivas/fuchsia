// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_INCLUDE_LIB_FDIO_PRIVATE_H_
#define LIB_FDIO_INCLUDE_LIB_FDIO_PRIVATE_H_

#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// WARNING: These APIs are subject to change

// __fdio_cleanpath cleans an input path, placing the output
// in out, which is a buffer of at least "PATH_MAX" bytes.
//
// 'outlen' returns the length of the path placed in out, and 'is_dir'
// is set to true if the returned path must be a directory.
zx_status_t __fdio_cleanpath(const char* in, char* out, size_t* outlen, bool* is_dir);

__END_CDECLS

#endif  // LIB_FDIO_INCLUDE_LIB_FDIO_PRIVATE_H_
