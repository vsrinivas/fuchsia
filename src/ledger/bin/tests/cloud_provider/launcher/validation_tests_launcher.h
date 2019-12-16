// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_TESTS_CLOUD_PROVIDER_LAUNCHER_VALIDATION_TESTS_LAUNCHER_H_
#define SRC_LEDGER_BIN_TESTS_CLOUD_PROVIDER_LAUNCHER_VALIDATION_TESTS_LAUNCHER_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/service_directory_provider.h>

#include <functional>
#include <string>

#include "src/ledger/lib/callback/auto_cleanable.h"

namespace cloud_provider {

// Helper for building launcher apps for the validation tests.
class ValidationTestsLauncher {
 public:
  // The constructor.
  //
  // |factory| is called to produce instances of the cloud provider under test.
  // It may return a component controller: when the cloud provider instance is
  // not used anymore (ie. the other end of the interface request is closed),
  // the component controller is closed, which terminates the cloud provider.
  ValidationTestsLauncher(async_dispatcher_t* dispatcher, sys::ComponentContext* component_context,
                          fit::function<fuchsia::sys::ComponentControllerPtr(
                              fidl::InterfaceRequest<fuchsia::ledger::cloud::CloudProvider>)>
                              factory);

  // Starts the tests.
  //
  // |arguments| are passed to the test binary.
  // |callback| is called after the tests are finished and passed the exit code
  //     of the test binary.
  void Run(const std::vector<std::string>& arguments, fit::function<void(int32_t)> callback);

 private:
  // Proxies requests from |request| to |proxied|, and terminates the component
  // controlled by |controller| when one of the ends closes the channel.
  class CloudProviderProxy {
   public:
    CloudProviderProxy(fidl::InterfacePtr<fuchsia::ledger::cloud::CloudProvider> proxied,
                       fidl::InterfaceRequest<fuchsia::ledger::cloud::CloudProvider> request,
                       fuchsia::sys::ComponentControllerPtr controller);
    ~CloudProviderProxy();
    void SetOnDiscardable(fit::closure on_discardable);
    bool IsDiscardable() const;

   private:
    fidl::Binding<fuchsia::ledger::cloud::CloudProvider> binding_;
    fidl::InterfacePtr<fuchsia::ledger::cloud::CloudProvider> proxied_;
    fuchsia::sys::ComponentControllerPtr controller_;
    fit::closure on_discardable_;
  };

  sys::ComponentContext* const component_context_;
  fit::function<fuchsia::sys::ComponentControllerPtr(
      fidl::InterfaceRequest<fuchsia::ledger::cloud::CloudProvider>)>
      factory_;
  sys::testing::ServiceDirectoryProvider service_directory_provider_;
  fuchsia::sys::ComponentControllerPtr validation_tests_controller_;
  fit::function<void(int32_t)> callback_;
  ledger::AutoCleanableSet<CloudProviderProxy> proxies_;
};

}  // namespace cloud_provider

#endif  // SRC_LEDGER_BIN_TESTS_CLOUD_PROVIDER_LAUNCHER_VALIDATION_TESTS_LAUNCHER_H_
