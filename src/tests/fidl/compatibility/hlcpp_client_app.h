// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Simple class that acts as a client of compatibility_test_service.Echo.
// In its own library so that both the C++ server and the compatibility test
// itself can use it.

#ifndef SRC_TESTS_FIDL_COMPATIBILITY_HLCPP_CLIENT_APP_H_
#define SRC_TESTS_FIDL_COMPATIBILITY_HLCPP_CLIENT_APP_H_

#include <fidl/test/compatibility/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>
#include <string>

namespace fidl {
namespace test {
namespace compatibility {

class EchoClientApp {
 public:
  EchoClientApp();

  EchoPtr& echo();

  void Connect();

 private:
  EchoClientApp(const EchoClientApp&) = delete;
  EchoClientApp& operator=(const EchoClientApp&) = delete;

  std::unique_ptr<sys::ComponentContext> context_;
  EchoPtr echo_;
};

}  // namespace compatibility
}  // namespace test
}  // namespace fidl

#endif  // SRC_TESTS_FIDL_COMPATIBILITY_HLCPP_CLIENT_APP_H_
