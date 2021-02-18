// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZXTEST_CPP_ZXTEST_PROD_H_
#define ZXTEST_CPP_ZXTEST_PROD_H_

// Include this file for "production" uses of zxtest.

// ZXTEST_FRIEND_TEST marks the given test as a friend for access to internals. For example, for
// the test:
//
//   TEST(MyTest, TheThing) {
//     MyClass my;
//     ENSURE_TRUE(my.SomePrivateFunction());
//   }
//
// One would use this in the class declaration:
//
//   class MyClass {
//     ...
//     ZXTEST_FRIEND_TEST(MyTest, TheThing);
//   };
//
// The way this is defined, the test must be in the same namespace as the class declaring the
// friend.
#define ZXTEST_FRIEND_TEST(TestCase, Test) friend class TestCase##_##Test##_Class

#endif  // ZXTEST_CPP_ZXTEST_PROD_H_
