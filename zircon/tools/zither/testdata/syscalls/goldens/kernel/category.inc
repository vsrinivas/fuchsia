// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// DO NOT EDIT. Generated from FIDL library zx by zither, a Fuchsia platform tool.

#define HAVE_SYSCALL_CATEGORY_blocking 1
SYSCALL_CATEGORY_BEGIN(blocking)
SYSCALL_IN_CATEGORY(nanosleep)
SYSCALL_CATEGORY_END(blocking)

#define HAVE_SYSCALL_CATEGORY_const 1
SYSCALL_CATEGORY_BEGIN(const)
SYSCALL_IN_CATEGORY(system_get_page_size)
SYSCALL_CATEGORY_END(const)

#define HAVE_SYSCALL_CATEGORY_next 1
SYSCALL_CATEGORY_BEGIN(next)
SYSCALL_IN_CATEGORY(syscall_next)
SYSCALL_CATEGORY_END(next)

#define HAVE_SYSCALL_CATEGORY_noreturn 1
SYSCALL_CATEGORY_BEGIN(noreturn)
SYSCALL_IN_CATEGORY(process_exit)
SYSCALL_CATEGORY_END(noreturn)

#define HAVE_SYSCALL_CATEGORY_test_category1 1
SYSCALL_CATEGORY_BEGIN(test_category1)
SYSCALL_IN_CATEGORY(syscall_test1)
SYSCALL_CATEGORY_END(test_category1)

#define HAVE_SYSCALL_CATEGORY_test_category2 1
SYSCALL_CATEGORY_BEGIN(test_category2)
SYSCALL_IN_CATEGORY(syscall_test2)
SYSCALL_CATEGORY_END(test_category2)

#define HAVE_SYSCALL_CATEGORY_vdsocall 1
SYSCALL_CATEGORY_BEGIN(vdsocall)
SYSCALL_IN_CATEGORY(clock_get_monotonic)
SYSCALL_IN_CATEGORY(system_get_page_size)
SYSCALL_CATEGORY_END(vdsocall)
