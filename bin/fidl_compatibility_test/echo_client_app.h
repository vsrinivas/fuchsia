// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Simple class that acts as a client of compatibility_test_service.Echo.
// In its own library so that both the C++ server and the compatibility test
// itself can use it.

#include <fuchsia/cpp/compatibility_test_service.h>
#include <zx/process.h>
#include <memory>
#include <string>

#include "lib/app/cpp/application_context.h"
#include "lib/svc/cpp/services.h"

namespace compatibility_test_service {

class EchoClientApp {
 public:
  EchoClientApp();

  EchoPtr& echo();

  void Start(std::string server_url);

 private:
  EchoClientApp(const EchoClientApp&) = delete;
  EchoClientApp& operator=(const EchoClientApp&) = delete;

  std::unique_ptr<component::ApplicationContext> context_;
  component::Services echo_provider_;
  component::ApplicationControllerPtr controller_;
  EchoPtr echo_;
};

}  // namespace compatibility_test_service
