// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTING_FIDL_ECHO_CLIENT_PLACEHOLDER_ECHO_CLIENT_APP_H_
#define SRC_TESTING_FIDL_ECHO_CLIENT_PLACEHOLDER_ECHO_CLIENT_APP_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#include <test/placeholders/cpp/fidl.h>

namespace echo {

// An implementation of the Echo client for use in tests.
class EchoClientApp {
 public:
  EchoClientApp();
  explicit EchoClientApp(std::unique_ptr<sys::ComponentContext> context);

  test::placeholders::EchoPtr& echo() { return echo_; }

  void Start(std::string server_url);

 private:
  EchoClientApp(const EchoClientApp&) = delete;
  EchoClientApp& operator=(const EchoClientApp&) = delete;

  std::unique_ptr<sys::ComponentContext> context_;
  fuchsia::sys::ComponentControllerPtr controller_;
  test::placeholders::EchoPtr echo_;
};

}  // namespace echo

#endif  // SRC_TESTING_FIDL_ECHO_CLIENT_PLACEHOLDER_ECHO_CLIENT_APP_H_
