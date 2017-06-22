// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Given one of the status codes defined in <magenta/errors.h> (MX_ERR_* or
// MX_OK), this function returns an identifier string for the status code.
//
// For example, mx_status_get_string(MX_ERR_TIMED_OUT) returns the string
// "MX_ERR_TIMED_OUT".
const char* _mx_status_get_string(mx_status_t status);
const char* mx_status_get_string(mx_status_t status);

#ifdef __cplusplus
}
#endif
