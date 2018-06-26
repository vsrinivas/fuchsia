// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_CLOUD_PROVIDER_VALIDATION_LAUNCHER_VALIDATION_TESTS_LAUNCHER_H_
#define LIB_CLOUD_PROVIDER_VALIDATION_LAUNCHER_VALIDATION_TESTS_LAUNCHER_H_

#include <functional>
#include <string>

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>

#include "lib/app/cpp/service_provider_impl.h"
#include "lib/app/cpp/startup_context.h"

namespace cloud_provider {

// Helper for building launcher apps for the validation tests.
class ValidationTestsLauncher {
 public:
  // The constructor.
  //
  // |factory| is called to produce instances of the cloud provider under test.
  ValidationTestsLauncher(
      fuchsia::sys::StartupContext* startup_context,
      std::function<
          void(fidl::InterfaceRequest<fuchsia::ledger::cloud::CloudProvider>)>
          factory);

  // Starts the tests.
  //
  // |arguments| are passed to the test binary.
  // |callback| is called after the tests are finished and passed the exit code
  //     of the test binary.
  void Run(const std::vector<std::string>& arguments,
           std::function<void(int32_t)> callback);

 private:
  fuchsia::sys::StartupContext* const startup_context_;
  std::function<void(
      fidl::InterfaceRequest<fuchsia::ledger::cloud::CloudProvider>)>
      factory_;
  fuchsia::sys::ServiceProviderImpl service_provider_impl_;
  fuchsia::sys::ComponentControllerPtr validation_tests_controller_;
  std::function<void(int32_t)> callback_;
};

}  // namespace cloud_provider

#endif  // LIB_CLOUD_PROVIDER_VALIDATION_LAUNCHER_VALIDATION_TESTS_LAUNCHER_H_
