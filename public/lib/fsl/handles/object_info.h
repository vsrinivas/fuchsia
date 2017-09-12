// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FSL_HANDLES_OBJECT_INFO_H_
#define LIB_FSL_HANDLES_OBJECT_INFO_H_

#include <magenta/syscalls/object.h>
#include <magenta/types.h>

#include <string>

#include "lib/fxl/fxl_export.h"

namespace fsl {

// Gets the kernel object id (koid) of the object associated with the handle.
// Returns |MX_KOID_INVALID| if the handle is invalid.
FXL_EXPORT mx_koid_t GetKoid(mx_handle_t handle);

// Gets the kernel object id (koid) of the objected related to the object
// associated with the handle. For example, if the object associated with the
// handle is a channel, this function returns the koid of the channel object at
// the opposite end. Returns |MX_KOID_INVALID| if the handle is invalid or
// the object associated with the handle has no related object.
FXL_EXPORT mx_koid_t GetRelatedKoid(mx_handle_t handle);

// Gets the name of a kernel object.
FXL_EXPORT std::string GetObjectName(mx_handle_t handle);

// Sets the name of a kernel object.
// Note: The kernel truncates names to |MX_MAX_NAME_LEN|.
FXL_EXPORT mx_status_t SetObjectName(mx_handle_t handle,
                                     const std::string& name);

// Gets the kernel object id of the current process.
FXL_EXPORT mx_koid_t GetCurrentProcessKoid();

// Gets the current process name.
FXL_EXPORT std::string GetCurrentProcessName();

// Gets the kernel object id of the current thread.
FXL_EXPORT mx_koid_t GetCurrentThreadKoid();

// Gets the current thread name.
FXL_EXPORT std::string GetCurrentThreadName();

// Sets the name of the current thread.
// Note: The kernel truncates names to |MX_MAX_NAME_LEN|.
FXL_EXPORT mx_status_t SetCurrentThreadName(const std::string& name);

}  // namespace fsl

#endif  // LIB_FSL_HANDLES_OBJECT_INFO_H_
