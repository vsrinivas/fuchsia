// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/syscalls.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma GCC visibility push(hidden)

// Accessors for system configuration info.
int mxr_get_nprocs(void);
int mxr_get_nprocs_conf(void);

#pragma GCC visibility pop

#ifdef __cplusplus
}
#endif
