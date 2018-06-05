// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_FIDL_ECHO2_SERVER_CPP_ECHO2_SERVER_APP_H_
#define GARNET_EXAMPLES_FIDL_ECHO2_SERVER_CPP_ECHO2_SERVER_APP_H_

#include <echo2/cpp/fidl.h>

#include "lib/app/cpp/startup_context.h"

namespace echo2 {

class EchoServerApp : public Echo {
 public:
  EchoServerApp();
  virtual void EchoString(fidl::StringPtr value, EchoStringCallback callback);

 private:
  EchoServerApp(const EchoServerApp&) = delete;
  EchoServerApp& operator=(const EchoServerApp&) = delete;

  std::unique_ptr<fuchsia::sys::StartupContext> context_;
  fidl::BindingSet<Echo> bindings_;
};

}  // namespace echo2

#endif  // GARNET_EXAMPLES_FIDL_ECHO2_SERVER_CPP_ECHO2_SERVER_APP_H_
