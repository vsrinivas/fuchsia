// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <magenta/syscalls/types.h>

#include <magenta/syscalls/pci.h>
#include <magenta/syscalls/resource.h>

__BEGIN_CDECLS

#include <magenta/gen-syscalls.h>

// Compatibility Wrappers for Deprecated Syscalls
mx_status_t mx_object_bind_exception_port(mx_handle_t obj_handle, mx_handle_t eport_handle, uint64_t key, bool debugger)
__attribute__((deprecated("use mx_task_bind_exception_port() instead.")));
mx_status_t _mx_object_bind_exception_port(mx_handle_t obj_handle, mx_handle_t eport_handle, uint64_t key, bool debugger)
__attribute__((deprecated("use _mx_task_bind_exception_port() instead.")));

__END_CDECLS
