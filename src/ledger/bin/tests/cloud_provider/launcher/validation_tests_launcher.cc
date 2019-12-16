// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/tests/cloud_provider/launcher/validation_tests_launcher.h"

#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/optional.h>

#include "src/ledger/lib/logging/logging.h"

namespace cloud_provider {

namespace {
constexpr char kValidationTestsUrl[] =
    "fuchsia-pkg://fuchsia.com/ledger_tests"
    "#meta/cloud_provider_validation_tests.cmx";
}  // namespace

ValidationTestsLauncher::CloudProviderProxy::CloudProviderProxy(
    fidl::InterfacePtr<fuchsia::ledger::cloud::CloudProvider> proxied,
    fidl::InterfaceRequest<fuchsia::ledger::cloud::CloudProvider> request,
    fuchsia::sys::ComponentControllerPtr controller)
    : binding_(proxied.get(), std::move(request)),
      proxied_(std::move(proxied)),
      controller_(std::move(controller)) {
  binding_.set_error_handler([this](zx_status_t status) {
    if (on_discardable_)
      on_discardable_();
  });
  proxied_.set_error_handler([this](zx_status_t status) {
    if (on_discardable_)
      on_discardable_();
  });
}

ValidationTestsLauncher::CloudProviderProxy::~CloudProviderProxy() = default;

void ValidationTestsLauncher::CloudProviderProxy::SetOnDiscardable(fit::closure on_discardable) {
  on_discardable_ = std::move(on_discardable);
}

bool ValidationTestsLauncher::CloudProviderProxy::IsDiscardable() const {
  return !binding_.is_bound() || !proxied_.is_bound();
}

ValidationTestsLauncher::ValidationTestsLauncher(
    async_dispatcher_t* dispatcher, sys::ComponentContext* component_context,
    fit::function<fuchsia::sys::ComponentControllerPtr(
        fidl::InterfaceRequest<fuchsia::ledger::cloud::CloudProvider>)>
        factory)
    : component_context_(component_context), factory_(std::move(factory)), proxies_(dispatcher) {
  service_directory_provider_.AddService<fuchsia::ledger::cloud::CloudProvider>(
      [this](fidl::InterfaceRequest<fuchsia::ledger::cloud::CloudProvider> request) {
        fidl::InterfacePtr<fuchsia::ledger::cloud::CloudProvider> proxied;
        auto controller = factory_(proxied.NewRequest());
        proxies_.emplace(std::move(proxied), std::move(request), std::move(controller));
      });
}

void ValidationTestsLauncher::Run(const std::vector<std::string>& arguments,
                                  fit::function<void(int32_t)> callback) {
  callback_ = std::move(callback);
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kValidationTestsUrl;
  fuchsia::sys::ServiceList service_list;
  service_list.names.push_back(fuchsia::ledger::cloud::CloudProvider::Name_);
  service_list.host_directory =
      service_directory_provider_.service_directory()->CloneChannel().TakeChannel();
  launch_info.additional_services = fidl::MakeOptional(std::move(service_list));
  launch_info.arguments = arguments;
  fuchsia::sys::LauncherPtr launcher;
  component_context_->svc()->Connect(launcher.NewRequest());
  launcher->CreateComponent(std::move(launch_info), validation_tests_controller_.NewRequest());

  validation_tests_controller_.events().OnTerminated =
      [this](int32_t return_code, fuchsia::sys::TerminationReason reason) {
        callback_(return_code);
      };
  validation_tests_controller_.set_error_handler([this](zx_status_t status) {
    LEDGER_LOG(ERROR) << "Lost connection to validation tests binary.";
    callback_(-1);
  });
}

}  // namespace cloud_provider
