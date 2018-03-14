// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/user_copy/user_ptr.h>
#include <lib/user_copy/testable_user_ptr.h>

namespace internal {

namespace testing {

// fake_user_ptr is an unsafe variant of user_ptr that is designed to be used in tests.
//
// It works in conjunction with testable_user_ptr.  See testable_user_ptr.h for an example.
//
// fake_user_ptr replaces user_ptr's copy routines with relaxed (unsafe) ones that can be used to
// access only kernel memory.
//
// fake_user_in_ptr should *never* be used outside of tests.

zx_status_t unsafe_copy(void* dst, const void* src, size_t len);

// TODO(ZX-1849): Find a way to prevent these traits from making their way into a release build. One
// idea would to be to reference a symbol that's found only in test builds.
class FakeUserCopyTraits {
public:
    constexpr static CopyFunc CopyTo = unsafe_copy;
    constexpr static CopyFunc CopyFrom = unsafe_copy;
};

template <typename T>
using fake_user_in_ptr = testable_user_in_ptr<T, FakeUserCopyTraits>;

template <typename T>
using fake_user_out_ptr = testable_user_out_ptr<T, FakeUserCopyTraits>;

template <typename T>
using fake_user_inout_ptr = testable_user_inout_ptr<T, FakeUserCopyTraits>;

template <typename T>
fake_user_in_ptr<T> make_fake_user_in_ptr(T* p) { return fake_user_in_ptr<T>(p); }

template <typename T>
fake_user_out_ptr<T> make_fake_user_out_ptr(T* p) { return fake_user_out_ptr<T>(p); }

template <typename T>
fake_user_inout_ptr<T> make_fake_user_inout_ptr(T* p) { return fake_user_inout_ptr<T>(p); }

}  // namespace testing

}  // namespace internal
