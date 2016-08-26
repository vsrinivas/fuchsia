// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Return the current process's own process handle.  The C library
// retains ownership of this handle and callers must duplicate it
// before using it in any destructive operation (close or transfer).
mx_handle_t mx_process_self(void);

#ifdef __cplusplus
}
#endif
