// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/tests/cloud_provider/launcher/validation_tests_launcher.h"

#include <lib/fidl/cpp/optional.h>

#include "src/lib/fxl/logging.h"

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
    if (on_empty_)
      on_empty_();
  });
  proxied_.set_error_handler([this](zx_status_t status) {
    if (on_empty_)
      on_empty_();
  });
}

ValidationTestsLauncher::CloudProviderProxy::~CloudProviderProxy(){};

void ValidationTestsLauncher::CloudProviderProxy::set_on_empty(
    fit::closure on_empty) {
  on_empty_ = std::move(on_empty);
}

ValidationTestsLauncher::ValidationTestsLauncher(
    sys::ComponentContext* component_context,
    fit::function<fuchsia::sys::ComponentControllerPtr(
        fidl::InterfaceRequest<fuchsia::ledger::cloud::CloudProvider>)>
        factory)
    : component_context_(component_context), factory_(std::move(factory)) {
  service_directory_provider_.AddService<fuchsia::ledger::cloud::CloudProvider>(
      [this](fidl::InterfaceRequest<fuchsia::ledger::cloud::CloudProvider>
                 request) {
        fidl::InterfacePtr<fuchsia::ledger::cloud::CloudProvider> proxied;
        auto controller = factory_(proxied.NewRequest());
        proxies_.emplace(std::move(proxied), std::move(request),
                         std::move(controller));
      });
}

void ValidationTestsLauncher::Run(const std::vector<std::string>& arguments,
                                  fit::function<void(int32_t)> callback) {
  callback_ = std::move(callback);
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kValidationTestsUrl;
  fuchsia::sys::ServiceList service_list;
  service_list.names.push_back(fuchsia::ledger::cloud::CloudProvider::Name_);
  service_list.host_directory = service_directory_provider_.service_directory()
                                    ->CloneChannel()
                                    .TakeChannel();
  launch_info.additional_services = fidl::MakeOptional(std::move(service_list));
  for (const auto& argument : arguments) {
    launch_info.arguments.push_back(argument);
  }
  fuchsia::sys::LauncherPtr launcher;
  component_context_->svc()->Connect(launcher.NewRequest());
  launcher->CreateComponent(std::move(launch_info),
                            validation_tests_controller_.NewRequest());

  validation_tests_controller_.events().OnTerminated =
      [this](int32_t return_code, fuchsia::sys::TerminationReason reason) {
        callback_(return_code);
      };
  validation_tests_controller_.set_error_handler([this](zx_status_t status) {
    FXL_LOG(ERROR) << "Lost connection to validation tests binary.";
    callback_(-1);
  });
}

}  // namespace cloud_provider
