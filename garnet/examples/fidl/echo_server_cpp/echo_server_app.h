// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_FIDL_ECHO_SERVER_CPP_ECHO_SERVER_APP_H_
#define GARNET_EXAMPLES_FIDL_ECHO_SERVER_CPP_ECHO_SERVER_APP_H_

#include <fidl/examples/echo/cpp/fidl.h>

#include <lib/sys/cpp/component_context.h>
#include <lib/fidl/cpp/binding_set.h>

namespace echo {

class EchoServerApp : public fidl::examples::echo::Echo {
 public:
  explicit EchoServerApp(bool quiet);
  virtual void EchoString(fidl::StringPtr value, EchoStringCallback callback);

 protected:
  EchoServerApp(std::unique_ptr<sys::ComponentContext> context, bool quiet);

 private:
  EchoServerApp(const EchoServerApp&) = delete;
  EchoServerApp& operator=(const EchoServerApp&) = delete;
  std::unique_ptr<sys::ComponentContext> context_;
  fidl::BindingSet<Echo> bindings_;
  bool quiet_;
};

}  // namespace echo

#endif  // GARNET_EXAMPLES_FIDL_ECHO_SERVER_CPP_ECHO_SERVER_APP_H_
