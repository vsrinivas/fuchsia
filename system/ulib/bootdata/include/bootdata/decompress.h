// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once


#pragma GCC visibility push(hidden)

#include <zircon/types.h>

// Decompress bootdata at offset of total size length into a new VMO
// On failure, errmsg is a human readable error description to provide
// more precise debug information.
zx_status_t decompress_bootdata(zx_handle_t vmar, zx_handle_t vmo,
                                size_t offset, size_t length,
                                zx_handle_t* out, const char** errmsg);

#pragma GCC visibility pop
