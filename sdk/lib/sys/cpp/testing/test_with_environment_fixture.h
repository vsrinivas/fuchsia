// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_CPP_TESTING_TEST_WITH_ENVIRONMENT_FIXTURE_H_
#define LIB_SYS_CPP_TESTING_TEST_WITH_ENVIRONMENT_FIXTURE_H_

#include <gtest/gtest.h>

#include "test_with_environment.h"

namespace gtest {
// An extension of TestWithEnvironment class that also implements the gtest
// fixture API. This fixture is meant to be used for multi-process tests.
//
// This allows, for example, a test to conveniently exercise FIDL, as FIDL
// bindings post waits to the thread-local dispatcher.
//
// Example:
//
//   #include "foo.fidl.h"
//
//   class TestFoo : public Foo {
//    public:
//      explicit TestFoo(InterfaceRequest<Foo> request)
//          : binding_(this, std::move(request) {}
//
//        // Foo implementation here.
//
//    private:
//     Binding<Foo> binding_;
//   };
//
//   // Creates a fixture that creates a message loop on this thread.
//   class TestBar : public ::gtest::TestWithEnvironmentFixture { /* ... */ };
//
//   TEST_F(TestBar, TestCase) {
//     // Do all FIDL-y stuff here and asynchronously quit.
//
//     RunLoop();
//
//     // Check results from FIDL-y stuff here.
//   }
class TestWithEnvironmentFixture : public ::sys::testing::TestWithEnvironment {};

// TODO(fxbug.dev/81468): Replace with the following, after removing gtest from
// |TestWithEnvironment|.
// class TestWithEnvironmentFixture : public ::sys::testing::TestWithEnvironment,
//                                   public ::testing::Test {};

}  // namespace gtest

#endif  // LIB_SYS_CPP_TESTING_TEST_WITH_ENVIRONMENT_FIXTURE_H_
