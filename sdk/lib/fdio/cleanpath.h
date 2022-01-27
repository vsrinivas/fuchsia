// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_CLEANPATH_H_
#define LIB_FDIO_CLEANPATH_H_

#include <stddef.h>
#include <zircon/types.h>

namespace fdio_internal {

// cleanpath cleans an input path, placing the output
// in out, which is a buffer of at least "PATH_MAX" bytes.
//
// 'outlen' returns the length of the path placed in out, and 'is_dir'
// is set to true if the returned path must be a directory.
zx_status_t cleanpath(const char* in, char* out, size_t* outlen, bool* is_dir);

}  // namespace fdio_internal

#endif  // LIB_FDIO_CLEANPATH_H_
