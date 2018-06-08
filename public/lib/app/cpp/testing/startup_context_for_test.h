// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_APP_CPP_TESTING_STARTUP_CONTEXT_FOR_TEST_H_
#define LIB_APP_CPP_TESTING_STARTUP_CONTEXT_FOR_TEST_H_

#include <fuchsia/sys/cpp/fidl.h>

#include "lib/app/cpp/startup_context.h"
#include "lib/svc/cpp/services.h"

namespace fuchsia {
namespace sys {
namespace testing {

// A fake |StartupContext| for testing.
// Does not allow publishing or accessing services outside of the test
// environment.
class StartupContextForTest : public StartupContext {
 public:
  StartupContextForTest(zx::channel service_root_client,
                        zx::channel service_root_server,
                        zx::channel directory_request_client,
                        zx::channel directory_request_server);
  static std::unique_ptr<StartupContextForTest> Create();

  // Defines the testing surface to be used in conjunction with
  // |StartupContextForTest|.
  class Controller {
   public:
    // Returns a |Services| that sees all public services added to the
    // |StartupContextForTest|.
    const Services& public_services() const {
      return context_->public_services_;
    }

   protected:
    Controller(StartupContextForTest* context) : context_(context){};

   private:
    StartupContextForTest* context_;
    friend class StartupContextForTest;
  };

  // Returns |Controller| for tests.
  // Tests should move the |StartupContextForTest| into the code under test, and
  // use the |Controller| to perform and verify interactions.
  const Controller& controller() const { return controller_; }

 private:
  Controller controller_;
  fuchsia::sys::Services public_services_;
  friend class Controller;
};

}  // namespace testing
}  // namespace sys
}  // namespace fuchsia

#endif  // LIB_APP_CPP_TESTING_STARTUP_CONTEXT_FOR_TEST_H_
