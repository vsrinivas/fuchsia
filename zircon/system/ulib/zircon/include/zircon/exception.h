// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/syscalls/exception.h>

#ifdef __cplusplus
extern "C" {
#endif

__EXPORT const char* _zx_exception_get_string(zx_excp_type_t exception);
__EXPORT const char* zx_exception_get_string(zx_excp_type_t exception);

#ifdef __cplusplus
}
#endif

