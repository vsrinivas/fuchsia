// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTING_FIDL_ECHO_SERVER_PLACEHOLDER_ECHO_SERVER_APP_H_
#define SRC_TESTING_FIDL_ECHO_SERVER_PLACEHOLDER_ECHO_SERVER_APP_H_

#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include <test/placeholders/cpp/fidl.h>

namespace echo {

// An implementation of the Echo service for use in tests.
class EchoServer : public test::placeholders::Echo {
 public:
  explicit EchoServer(bool quiet);

  void EchoString(fidl::StringPtr value, EchoStringCallback callback) override;

 private:
  bool quiet_;
};

// An application class to house an `EchoServer` in a `ComponentContext`.
class EchoServerApp {
 public:
  explicit EchoServerApp(bool quiet);

 protected:
  EchoServerApp(std::unique_ptr<sys::ComponentContext> context, bool quiet);

 private:
  using Echo = test::placeholders::Echo;

  EchoServerApp(const EchoServerApp&) = delete;
  EchoServerApp& operator=(const EchoServerApp&) = delete;
  std::unique_ptr<EchoServer> service_;
  std::unique_ptr<sys::ComponentContext> context_;
  fidl::BindingSet<Echo> bindings_;
};

}  // namespace echo

#endif  // SRC_TESTING_FIDL_ECHO_SERVER_PLACEHOLDER_ECHO_SERVER_APP_H_
