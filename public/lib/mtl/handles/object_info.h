// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTL_OBJECT_INFO_H_
#define LIB_MTL_OBJECT_INFO_H_

#include <magenta/syscalls/types.h>

#include <string>

namespace mtl {

// Gets the kernel object id (koid) of the object associated with the handle.
// Returns |MX_KOID_INVALID| if the handle is invalid.
mx_koid_t GetKoid(mx_handle_t handle);

// Gets the name of a kernel object.
std::string GetObjectName(mx_handle_t handle);

// Sets the name of a kernel object.
// Note: The kernel truncates names to |MX_MAX_NAME_LEN|.
mx_status_t SetObjectName(mx_handle_t handle, const std::string& name);

// Gets the kernel object id of the current process.
mx_koid_t GetCurrentProcessKoid();

// Gets the current process name.
std::string GetCurrentProcessName();

// Gets the kernel object id of the current thread.
mx_koid_t GetCurrentThreadKoid();

// Gets the current thread name.
std::string GetCurrentThreadName();

// Sets the name of the current thread.
// Note: The kernel truncates names to |MX_MAX_NAME_LEN|.
mx_status_t SetCurrentThreadName(const std::string& name);

}  // namespace mtl

#endif  // LIB_MTL_OBJECT_INFO_H_
