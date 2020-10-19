// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CTS_TESTS_HELLO_WORLD_HELLO_WORLD_UTIL_H_
#define CTS_TESTS_HELLO_WORLD_HELLO_WORLD_UTIL_H_
#include <string>

namespace {
class HelloWorldUtil {
 public:
  static std::string get_hello_world() { return "Hello, World!"; }
};
}  // namespace
#endif  // CTS_TESTS_HELLO_WORLD_HELLO_WORLD_UTIL_H_
