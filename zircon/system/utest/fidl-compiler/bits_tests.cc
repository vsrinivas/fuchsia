// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include "test_library.h"

namespace {

bool GoodBitsTestSimple() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

bits Fruit : uint64 {
    ORANGE = 1;
    APPLE = 2;
    BANANA = 4;
};
)FIDL");
    ASSERT_TRUE(library.Compile());

    END_TEST;
}

bool BadBitsTestSigned() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

bits Fruit : int64 {
    ORANGE = 1;
    APPLE = 2;
    BANANA = 4;
};
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_STR_STR(errors[0].data(),
                   "may only be of unsigned integral primitive type");

    END_TEST;
}

bool BadBitsTestWithNonUniqueValues() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

bits Fruit : uint64 {
    ORANGE = 1;
    APPLE = 1;
};
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "value of member APPLE conflicts with previously declared "
                                      "member ORANGE in the bits Fruit");

    END_TEST;
}

bool BadBitsTestWithNonUniqueValuesOutOfLine() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

bits Fruit {
    ORANGE = FOUR;
    APPLE = TWO_SQUARED;
};

const uint32 FOUR = 4;
const uint32 TWO_SQUARED = 4;
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "value of member APPLE conflicts with previously declared "
                                      "member ORANGE in the bits Fruit");

    END_TEST;
}

bool BadBitsTestUnsignedWithNegativeMember() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

bits Fruit : uint64 {
    ORANGE = 1;
    APPLE = -2;
};
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_GE(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "-2 cannot be interpreted as type uint64");

    END_TEST;
}

bool BadBitsTestMemberOverflow() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

bits Fruit : uint8 {
    ORANGE = 1;
    APPLE = 256;
};
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_GE(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "256 cannot be interpreted as type uint8");

    END_TEST;
}

bool BadBitsTestDuplicateMember() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

bits Fruit : uint64 {
    ORANGE = 1;
    APPLE = 2;
    ORANGE = 4;
};
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_GE(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "name of member ORANGE conflicts with previously declared "
                                      "member in the bits Fruit");

    END_TEST;
}

bool GoodBitsTestKeywordNames() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

bits Fruit : uint64 {
    library = 1;
    bits = 2;
    uint64 = 4;
};
)FIDL");
    ASSERT_TRUE(library.Compile());

    END_TEST;
}

bool BadBitsTestNonPowerOfTwo() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

bits non_power_of_two : uint64 {
    three = 3;
};
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_GE(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "bits members must be powers of two");

    END_TEST;
}

bool GoodBitsTestShape() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

bits Bits16 : uint16 {
    VALUE = 1;
};

bits BitsImplicit {
    VALUE = 1;
};
)FIDL");
    ASSERT_TRUE(library.Compile());

    auto bits16 = library.LookupBits("Bits16");
    EXPECT_NONNULL(bits16);
    EXPECT_EQ(bits16->typeshape.Size(), 2);
    EXPECT_EQ(bits16->typeshape.Alignment(), 2);
    EXPECT_EQ(bits16->typeshape.MaxOutOfLine(), 0);

    auto bits_implicit = library.LookupBits("BitsImplicit");
    EXPECT_NONNULL(bits_implicit);
    EXPECT_EQ(bits_implicit->typeshape.Size(), 4);
    EXPECT_EQ(bits_implicit->typeshape.Alignment(), 4);
    EXPECT_EQ(bits_implicit->typeshape.MaxOutOfLine(), 0);

    END_TEST;
}

bool GoodBitsTestMask() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

bits Life {
    A = 0b000010;
    B = 0b001000;
    C = 0b100000;
};
)FIDL");
    ASSERT_TRUE(library.Compile());

    auto bits = library.LookupBits("Life");
    ASSERT_NONNULL(bits);
    EXPECT_EQ(bits->mask, 42);

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(bits_tests)

RUN_TEST(GoodBitsTestSimple)
RUN_TEST(BadBitsTestSigned)
RUN_TEST(BadBitsTestWithNonUniqueValues)
RUN_TEST(BadBitsTestWithNonUniqueValuesOutOfLine)
RUN_TEST(BadBitsTestUnsignedWithNegativeMember)
RUN_TEST(BadBitsTestMemberOverflow)
RUN_TEST(BadBitsTestDuplicateMember)
RUN_TEST(GoodBitsTestKeywordNames)
RUN_TEST(GoodBitsTestShape)
RUN_TEST(BadBitsTestNonPowerOfTwo)
RUN_TEST(GoodBitsTestMask)
END_TEST_CASE(bits_tests)
