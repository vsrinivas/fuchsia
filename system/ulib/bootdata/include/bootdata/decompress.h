// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once


#pragma GCC visibility push(hidden)

#include <magenta/types.h>

// If the VMO holds a compressed bootdata, returns a handle to a new VMO with
// the decompressed data and consumes the original VMO handle. Otherwise returns
// the original handle.
mx_handle_t decompress_vmo(mx_handle_t log, mx_handle_t vmar, mx_handle_t vmo);

// Function prototypes for logging. These are used rather than stdio so that
// userboot can log when stdio is not available.
extern void print(mx_handle_t log, const char* s, ...) __attribute__((sentinel));
extern void fail(mx_handle_t log, mx_status_t status, const char* msg);

#pragma GCC visibility pop
