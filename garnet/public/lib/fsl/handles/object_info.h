// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FSL_HANDLES_OBJECT_INFO_H_
#define LIB_FSL_HANDLES_OBJECT_INFO_H_

#include <lib/zx/time.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#include <string>
#include <utility>

#include "lib/fxl/fxl_export.h"

namespace fsl {

// Gets the kernel object id (koid) of the object associated with the handle.
// Returns |ZX_KOID_INVALID| if the handle is invalid.
FXL_EXPORT zx_koid_t GetKoid(zx_handle_t handle);

// Gets the kernel object id (koid) of the object related to the object
// associated with the handle. For example, if the object associated with the
// handle is a channel, this function returns the koid of the channel object at
// the opposite end.
//
// Returns |ZX_KOID_INVALID| if the handle is invalid or the object associated
// with the handle has no related object.
//
// Note that closing all of the handles to the related object will not free it
// immediately, as long as handles to its partner remain.  The return value will
// still be a valid koid in this case.
FXL_EXPORT zx_koid_t GetRelatedKoid(zx_handle_t handle);

// Gets the kernel object ids (koids) of the object associated with the handle
// and of the object related to the handle's object.
//
// For example, if the object associated with the handle is an eventpair, this
// function returns the koids of each end.
//
// The return value is as if std::pair(GetKoid(), GetRelatedKoid()) was called.
//
// The first slot of the std::pair contains |ZX_KOID_INVALID| if the handle is
// invalid.   The second slot of the std::pair contains |ZX_KOID_INVALID| if the
// handle is invalid, or the object associated with the handle has no related
// object.
//
// Note that closing all of the handles to the related object will not free it
// immediately, as long as handles to its partner remain.  The second slot of
// the std::pair will contain a valid koid in this case.
FXL_EXPORT std::pair<zx_koid_t, zx_koid_t> GetKoids(zx_handle_t handle);

// Gets the type of a kernel object.
FXL_EXPORT zx_obj_type_t GetType(zx_handle_t handle);

// Gets the name of a kernel object.
FXL_EXPORT std::string GetObjectName(zx_handle_t handle);

// Sets the name of a kernel object.
// Note: The kernel truncates names to |ZX_MAX_NAME_LEN|.
FXL_EXPORT zx_status_t SetObjectName(zx_handle_t handle,
                                     const std::string& name);

// Gets the kernel object id of the current process.
FXL_EXPORT zx_koid_t GetCurrentProcessKoid();

// Gets the current process name.
FXL_EXPORT std::string GetCurrentProcessName();

// Gets the kernel object id of the current thread.
FXL_EXPORT zx_koid_t GetCurrentThreadKoid();

// Gets the current thread name.
FXL_EXPORT std::string GetCurrentThreadName();

// Sets the name of the current thread.
// Note: The kernel truncates names to |ZX_MAX_NAME_LEN|.
FXL_EXPORT zx_status_t SetCurrentThreadName(const std::string& name);

// Gets the total runtime of the current thread.
FXL_EXPORT zx::duration GetCurrentThreadTotalRuntime();

// Gets the total size of mapped memory ranges in the current process in bytes.
// Not all will be backed by physical memory.
FXL_EXPORT size_t GetCurrentProcessMemoryMappedBytes();

// Gets the committed memory that is only mapped into the current process in
// bytes.
FXL_EXPORT size_t GetCurrentProcessMemoryPrivateBytes();

// Gets the committed memory that is mapped into the current process and at
// least one other process.
FXL_EXPORT size_t GetCurrentProcessMemorySharedBytes();

// Gets a number that estimates the fraction of shared bytes that the current
// process is responsible for keeping alive.
FXL_EXPORT size_t GetCurrentProcessMemoryScaledSharedBytes();

}  // namespace fsl

#endif  // LIB_FSL_HANDLES_OBJECT_INFO_H_
