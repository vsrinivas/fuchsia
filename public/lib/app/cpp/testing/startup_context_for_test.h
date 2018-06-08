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
// environment. Exposes controllers that can be used by test cases to verify
// context interactions under test.
class StartupContextForTest : public StartupContext {
 public:
  StartupContextForTest(zx::channel service_root_client,
                        zx::channel service_root_server,
                        zx::channel directory_request_client,
                        zx::channel directory_request_server);
  static std::unique_ptr<StartupContextForTest> Create();

  const Services& services() const { return services_; }

 private:
  fuchsia::sys::Services services_;
};

}  // namespace testing
}  // namespace sys
}  // namespace fuchsia

#endif  // LIB_APP_CPP_TESTING_STARTUP_CONTEXT_FOR_TEST_H_
