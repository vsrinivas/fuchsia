// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <trace-engine/fields.h>

#include <unittest/unittest.h>

namespace {

static bool field_get_set_test(void) {
    BEGIN_TEST;

    uint64_t value(0);

    trace::Field<0, 0>::Set(value, uint8_t(1));
    trace::Field<1, 1>::Set(value, uint8_t(1));
    trace::Field<2, 2>::Set(value, uint8_t(1));
    trace::Field<3, 3>::Set(value, uint8_t(1));
    trace::Field<4, 4>::Set(value, uint8_t(1));
    trace::Field<5, 5>::Set(value, uint8_t(1));
    trace::Field<6, 6>::Set(value, uint8_t(1));
    trace::Field<7, 7>::Set(value, uint8_t(1));

    EXPECT_EQ(uint8_t(-1), value);
    value = 0;
    trace::Field<0, 2>::Set(value, uint8_t(7));
    EXPECT_EQ(uint8_t(7), value);
    trace::Field<0, 2>::Set(value, uint8_t(0));
    EXPECT_EQ(uint8_t(0), value);

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(types)
RUN_TEST(field_get_set_test)
END_TEST_CASE(types)
