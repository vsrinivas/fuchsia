// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_COMPONENT2_CPP_TESTING_STARTUP_CONTEXT_FOR_TEST_H_
#define LIB_COMPONENT2_CPP_TESTING_STARTUP_CONTEXT_FOR_TEST_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/component2/cpp/startup_context.h>

#include "lib/component2/cpp/testing/service_directory_for_test.h"

namespace component2 {
namespace testing {

// A fake |StartupContext| for unit testing.
// Does not allow publishing or accessing services outside of this object.
class StartupContextForTest final : public component2::StartupContext {
 public:
  StartupContextForTest(std::shared_ptr<ServiceDirectoryForTest> svc,
                        fuchsia::io::DirectoryPtr directory_ptr,
                        async_dispatcher_t* dispatcher = nullptr);

  ~StartupContextForTest() override;

  static std::unique_ptr<StartupContextForTest> Create(
      async_dispatcher_t* dispatcher = nullptr);

  // Points to outgoing root directory of outgoing directory, test can get it
  // and try to connect to internal directories/objects/files/services to test
  // code which published them.
  fuchsia::io::DirectoryPtr& outgoing_directory_ptr() {
    return outgoing_directory_ptr_;
  }

  // Points to public directory of outgoing directory, test can get it and try
  // to connect to a public service published by code under test.
  fuchsia::io::DirectoryPtr& public_directory_ptr() {
    return public_directory_ptr_;
  }

  // This can be used to get fake service directory and inject services which
  // can be accessed by code under test.
  //
  // # Example
  //
  // ```
  // fidl::BindingSet<fuchsia::foo::Controller> bindings;
  // context()->service_directory_for_test()->AddService(bindings.GetHandler(this));
  // ```
  const std::shared_ptr<ServiceDirectoryForTest>& service_directory_for_test()
      const {
    return fake_svc_;
  };

 private:
  fuchsia::io::DirectoryPtr outgoing_directory_ptr_;
  fuchsia::io::DirectoryPtr public_directory_ptr_;
  std::shared_ptr<ServiceDirectoryForTest> fake_svc_;
};

}  // namespace testing
}  // namespace component2

#endif  // LIB_COMPONENT2_CPP_TESTING_STARTUP_CONTEXT_FOR_TEST_H_
