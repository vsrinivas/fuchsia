// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/macros.h>

#include <unittest/unittest.h>

namespace {

struct Empty {};

struct Full {
    using Typedef = int;
    bool Test(bool flag);
};

DECLARE_HAS_MEMBER_FN(has_fn_true, Test);
DECLARE_HAS_MEMBER_FN(has_fn_false, Nonexistent);
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_fn_sig_true, Test, bool (C::*)(bool));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_fn_sig_false, Test, bool (C::*)(bool) const);
DECLARE_HAS_MEMBER_TYPE(has_type_true, Typedef);
DECLARE_HAS_MEMBER_TYPE(has_type_false, Nonexistent);

// This will compile down to nothing but we want to make sure it is built
bool macro_test() {
    BEGIN_TEST;

    static_assert(has_fn_true_v<Full>);
    static_assert(!has_fn_false_v<Full>);
    static_assert(!has_fn_true_v<Empty>);
    static_assert(!has_fn_false_v<Empty>);

    static_assert(has_fn_sig_true_v<Full>);
    static_assert(!has_fn_sig_false_v<Full>);
    static_assert(!has_fn_sig_true_v<Empty>);
    static_assert(!has_fn_sig_false_v<Empty>);

    static_assert(has_type_true_v<Full>);
    static_assert(!has_type_false_v<Full>);
    static_assert(!has_type_true_v<Empty>);
    static_assert(!has_type_false_v<Empty>);

    END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(compile_time_macro_tests)
RUN_TEST(macro_test)
END_TEST_CASE(compile_time_macro_tests)
