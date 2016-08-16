// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma GCC visibility push(hidden)

// Return a human-readable description of status.
const char* mx_strstatus(mx_status_t status);

#pragma GCC visibility pop

#ifdef __cplusplus
}
#endif
