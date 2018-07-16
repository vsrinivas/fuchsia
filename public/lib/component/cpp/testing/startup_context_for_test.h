// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_APP_CPP_TESTING_STARTUP_CONTEXT_FOR_TEST_H_
#define LIB_APP_CPP_TESTING_STARTUP_CONTEXT_FOR_TEST_H_

#include <fs/pseudo-dir.h>
#include <fuchsia/sys/cpp/fidl.h>

#include "lib/component/cpp/startup_context.h"
#include "lib/component/cpp/testing/fake_launcher.h"
#include "lib/svc/cpp/services.h"

namespace component {
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
    const Services& outgoing_public_services() const {
      return context_->outgoing_public_services_;
    }

    FakeLauncher& fake_launcher() const { return context_->fake_launcher_; }

   protected:
    Controller(StartupContextForTest* context) : context_(context){};

   private:
    StartupContextForTest* context_;
    friend class StartupContextForTest;
  };

  // Returns |Controller| for tests.
  // Tests should move the |StartupContextForTest| into the code under test, and
  // use the |Controller| to perform and verify interactions.
  Controller& controller() { return controller_; }

 private:
  friend class Controller;
  Controller controller_;

  component::Services outgoing_public_services_;
  fs::SynchronousVfs service_root_vfs_;
  fbl::RefPtr<fs::PseudoDir> service_root_dir_;
  FakeLauncher fake_launcher_;

  static zx::channel ChannelConnectAt(zx_handle_t root, const char* path);
};

}  // namespace testing
}  // namespace component

#endif  // LIB_APP_CPP_TESTING_STARTUP_CONTEXT_FOR_TEST_H_
