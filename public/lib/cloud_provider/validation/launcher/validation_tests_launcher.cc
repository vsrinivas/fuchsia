// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/public/lib/cloud_provider/validation/launcher/validation_tests_launcher.h"

#include "lib/fxl/logging.h"

namespace cloud_provider {

namespace {
constexpr char kValidationTestsUrl[] =
    "/system/test/disabled/cloud_provider_validation_tests";
}  // namespace

ValidationTestsLauncher::ValidationTestsLauncher(
    app::ApplicationContext* application_context,
    std::function<void(fidl::InterfaceRequest<CloudProvider>)> factory)
    : application_context_(application_context), factory_(std::move(factory)) {
  service_provider_impl_.AddService<cloud_provider::CloudProvider>(
      [this](fidl::InterfaceRequest<cloud_provider::CloudProvider> request) {
        factory_(std::move(request));
      });
}

void ValidationTestsLauncher::Run(std::function<void(int32_t)> callback) {
  callback_ = std::move(callback);
  auto launch_info = app::ApplicationLaunchInfo::New();
  launch_info->url = kValidationTestsUrl;
  auto service_list = app::ServiceList::New();
  service_list->names.push_back(cloud_provider::CloudProvider::Name_);
  service_provider_impl_.AddBinding(service_list->provider.NewRequest());
  launch_info->additional_services = std::move(service_list);

  application_context_->launcher()->CreateApplication(
      std::move(launch_info), validation_tests_controller_.NewRequest());

  validation_tests_controller_->Wait(
      [this](int32_t return_code) { callback_(return_code); });
  validation_tests_controller_.set_connection_error_handler([this] {
    FXL_LOG(ERROR) << "Lost connection to validation tests binary.";
    callback_(-1);
  });
}

}  // namespace cloud_provider
