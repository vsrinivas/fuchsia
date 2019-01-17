// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zxtest/base/runner.h>
#include <zxtest/base/test.h>

// Macro definitions for usage within CPP.
#define RUN_ALL_TESTS(argc, argv) zxtest::RunAllTests(argc, argv)

#define _ZXTEST_TEST_REF(TestCase, Test) TestCase##_##Test##_Ref

#define _ZXTEST_DEFAULT_FIXTURE ::zxtest::Test

#define _ZXTEST_TEST_CLASS(TestCase, Test) TestCase##_##Test##_Class

#define _ZXTEST_TEST_CLASS_DECL(Fixture, TestClass) \
    class TestClass : public Fixture {              \
    public:                                         \
        TestClass() = default;                      \
        ~TestClass() final = default;               \
                                                    \
    private:                                        \
        void TestBody() final;                      \
    }

#define _ZXTEST_BEGIN_TEST_BODY(TestClass) void TestClass::TestBody()

#define _ZXTEST_REGISTER(TestCase, Test, Fixture)                                                 \
    _ZXTEST_TEST_CLASS_DECL(Fixture, _ZXTEST_TEST_CLASS(TestCase, Test));                         \
    [[maybe_unused]] static zxtest::TestRef _ZXTEST_TEST_REF(TestCase, Test) =                    \
        zxtest::Runner::GetInstance()->RegisterTest<Fixture, _ZXTEST_TEST_CLASS(TestCase, Test)>( \
            #TestCase, #Test, __FILE__, __LINE__);                                                \
    _ZXTEST_BEGIN_TEST_BODY(_ZXTEST_TEST_CLASS(TestCase, Test))

#define TEST(TestCase, Test) _ZXTEST_REGISTER(TestCase, Test, _ZXTEST_DEFAULT_FIXTURE)

#define TEST_F(TestCase, Test) _ZXTEST_REGISTER(TestCase, Test, TestCase)
