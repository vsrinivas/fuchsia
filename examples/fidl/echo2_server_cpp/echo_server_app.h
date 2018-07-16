// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_FIDL_ECHO2_SERVER_CPP_ECHO_SERVER_APP_H_
#define GARNET_EXAMPLES_FIDL_ECHO2_SERVER_CPP_ECHO_SERVER_APP_H_

#include <fidl/examples/echo/cpp/fidl.h>

#include "lib/component/cpp/startup_context.h"

namespace echo2 {

class EchoServerApp : public fidl::examples::echo::Echo {
 public:
  explicit EchoServerApp(bool quiet);
  virtual void EchoString(fidl::StringPtr value, EchoStringCallback callback);

 protected:
  EchoServerApp(std::unique_ptr<component::StartupContext> context,
                bool quiet);

 private:
  EchoServerApp(const EchoServerApp&) = delete;
  EchoServerApp& operator=(const EchoServerApp&) = delete;
  std::unique_ptr<component::StartupContext> context_;
  fidl::BindingSet<Echo> bindings_;
  bool quiet_;
};

}  // namespace echo2

#endif  // GARNET_EXAMPLES_FIDL_ECHO2_SERVER_CPP_ECHO_SERVER_APP_H_
