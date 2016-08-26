// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/types.h>
#include <magenta/syscalls-types.h>
#include <mxtl/user_ptr.h>

// Please don't put CDECLS here. We want the stricter declaration matching
// rules of C++.
#define MAGENTA_DDKCALL_DEF(a...) MAGENTA_SYSCALL_DEF(a)
#define MAGENTA_SYSCALL_DEF(nargs64, nargs32, n, ret, name, args...) ret sys_##name(args);
// On the kernel side, we define the type-safe user_ptr<> for syscall user pointer params.
#define USER_PTR(type) mxtl::user_ptr<type>
#include <magenta/syscalls.inc>

// Determines if this handle is to a Resource object.
// Used to provide access to privileged syscalls.
// Later, Resource objects will be finer-grained.
mx_status_t validate_resource_handle(mx_handle_t handle);