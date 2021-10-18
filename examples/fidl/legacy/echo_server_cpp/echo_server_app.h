// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXAMPLES_FIDL_LEGACY_ECHO_SERVER_CPP_ECHO_SERVER_APP_H_
#define EXAMPLES_FIDL_LEGACY_ECHO_SERVER_CPP_ECHO_SERVER_APP_H_

#include <fidl/examples/echo/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

namespace echo {

// An implementation of the fidl.examples.echo.Echo service. The service implementation is separated
// from the app class to simplify testing of service logic.
class EchoServer : public fidl::examples::echo::Echo {
 public:
  explicit EchoServer(bool quiet);

  virtual void EchoString(fidl::StringPtr value, EchoStringCallback callback);

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
  using Echo = fidl::examples::echo::Echo;

  EchoServerApp(const EchoServerApp&) = delete;
  EchoServerApp& operator=(const EchoServerApp&) = delete;
  std::unique_ptr<EchoServer> service_;
  std::unique_ptr<sys::ComponentContext> context_;
  fidl::BindingSet<Echo> bindings_;
};

}  // namespace echo

#endif  // EXAMPLES_FIDL_LEGACY_ECHO_SERVER_CPP_ECHO_SERVER_APP_H_
