// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/types.h>
#include <magenta/syscalls-types.h>
#include <lib/user_copy/user_ptr.h>

// Please don't put CDECLS here. We want the stricter declaration matching
// rules of C++.
#define MAGENTA_SYSCALL_DEF(nargs64, nargs32, n, ret, name, args...) ret sys_##name(args);
// On the kernel side, we define the type-safe user_ptr<> for syscall user pointer params.
#define USER_PTR(type) user_ptr<type>
#include <magenta/syscalls.inc>
