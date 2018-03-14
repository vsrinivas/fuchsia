// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/user_copy/user_ptr.h>

// testable_user_ptr is a user_ptr that simplifies testing of code dealing with userspace addresses.
//
// It works in conjunction with fake_user_ptr.  The typical use case is you have a function that
// takes a user_ptr and you want to write a test for it that runs in kernel space.  Say you have a
// function foo:
//
//     bool foo(user_in_ptr<const void> in);
//
// As is, you can't easily test foo() from kernel space because 'in' must be a userspace address.
// Here's where testable_user_ptr comes in.  You can change foo's signature to take a
// testable_user_ptr instead:
//
//     template <typename UCT>
//     void foo(testable_user_in_ptr<const void, UCT> in);
//
// All the call sites should continue to work as usual.  Now, in your test code where you call
// foo(), use a fake_user_ptr instead:
//
//     EXPECT_TRUE(foo(internal::testing::make_fake_user_in_ptr(...)));
//
// Note: fake_user_ptr should *never* be used outside of test code because it lacks the safety
// mechanisms of user_ptr.

template <typename T, typename UCT>
using testable_user_in_ptr = internal::user_ptr<T, internal::kIn, UCT>;

template <typename T, typename UCT>
using testable_user_out_ptr = internal::user_ptr<T, internal::kOut, UCT>;

template <typename T, typename UCT>
using testable_user_inout_ptr = internal::user_ptr<T, internal::kInOut, UCT>;
