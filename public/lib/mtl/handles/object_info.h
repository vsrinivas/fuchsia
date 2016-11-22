// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTL_OBJECT_INFO_H_
#define LIB_MTL_OBJECT_INFO_H_

#include <magenta/syscalls/types.h>

namespace mtl {

// Gets the kernel object id (koid) of the object associated with the handle.
// Returns |MX_KOID_INVALID| if the handle is invalid.
mx_koid_t GetKoid(mx_handle_t handle);

// Gets the kernel object id of the current process.
mx_koid_t GetCurrentProcessKoid();

// Gets the kernel object id of the current thread.
mx_koid_t GetCurrentThreadKoid();

}  // namespace mtl

#endif  // LIB_MTL_OBJECT_INFO_H_
