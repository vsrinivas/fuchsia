// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Return the identifier (macro name in <magenta/fuchsia-types.h> without
// "ERR_" prefix) for the status number.
const char* _mx_status_get_string(mx_status_t status);
const char* mx_status_get_string(mx_status_t status);

#ifdef __cplusplus
}
#endif
