// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include <memory>

#include "src/developer/forensics/feedback_data/main_service.h"
#include "src/developer/forensics/utils/component/component.h"

namespace forensics {
namespace feedback_data {

int main() {
  syslog::SetTags({"forensics", "feedback"});

  forensics::component::Component component;

  std::unique_ptr<MainService> main_service =
      MainService::TryCreate(component.Dispatcher(), component.Services(), component.InspectRoot());
  if (!main_service) {
    return EXIT_FAILURE;
  }

  main_service->SpawnSystemLogRecorder();

  // fuchsia.feedback.ComponentDataRegister
  component.AddPublicService(
      ::fidl::InterfaceRequestHandler<fuchsia::feedback::ComponentDataRegister>(
          [&main_service](
              ::fidl::InterfaceRequest<fuchsia::feedback::ComponentDataRegister> request) {
            main_service->HandleComponentDataRegisterRequest(std::move(request));
          }));

  // fuchsia.feedback.DataProvider
  component.AddPublicService(::fidl::InterfaceRequestHandler<fuchsia::feedback::DataProvider>(
      [&main_service](::fidl::InterfaceRequest<fuchsia::feedback::DataProvider> request) {
        main_service->HandleDataProviderRequest(std::move(request));
      }));

  // fuchsia.feedback.DevideIdProvider
  component.AddPublicService(::fidl::InterfaceRequestHandler<fuchsia::feedback::DeviceIdProvider>(
      [&main_service](::fidl::InterfaceRequest<fuchsia::feedback::DeviceIdProvider> request) {
        main_service->HandleDeviceIdProviderRequest(std::move(request));
      }));

  component.RunLoop();

  return EXIT_SUCCESS;
}

}  // namespace feedback_data
}  // namespace forensics
