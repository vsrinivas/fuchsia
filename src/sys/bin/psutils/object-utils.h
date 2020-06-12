// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_BIN_PSUTILS_OBJECT_UTILS_H_
#define SRC_SYS_BIN_PSUTILS_OBJECT_UTILS_H_

#include <zircon/types.h>

const char* obj_type_get_name(zx_obj_type_t type);

#endif  // SRC_SYS_BIN_PSUTILS_OBJECT_UTILS_H_
