// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/inspect/cpp/component.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include <memory>

#include "src/developer/forensics/feedback_data/main_service.h"

int main(int argc, const char** argv) {
  using namespace ::forensics::feedback_data;

  syslog::SetTags({"feedback"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  auto inspector = std::make_unique<sys::ComponentInspector>(context.get());
  inspect::Node& root_node = inspector->root();

  std::unique_ptr<MainService> main_service =
      MainService::TryCreate(loop.dispatcher(), context->svc(), &root_node);
  if (!main_service) {
    return EXIT_FAILURE;
  }

  main_service->SpawnSystemLogRecorder();

  // fuchsia.feedback.ComponentDataRegister
  context->outgoing()->AddPublicService(
      ::fidl::InterfaceRequestHandler<fuchsia::feedback::ComponentDataRegister>(
          [&main_service](
              ::fidl::InterfaceRequest<fuchsia::feedback::ComponentDataRegister> request) {
            main_service->HandleComponentDataRegisterRequest(std::move(request));
          }));

  // fuchsia.feedback.DataProvider
  context->outgoing()->AddPublicService(
      ::fidl::InterfaceRequestHandler<fuchsia::feedback::DataProvider>(
          [&main_service](::fidl::InterfaceRequest<fuchsia::feedback::DataProvider> request) {
            main_service->HandleDataProviderRequest(std::move(request));
          }));

  // fuchsia.feedback.DevideIdProvider
  context->outgoing()->AddPublicService(
      ::fidl::InterfaceRequestHandler<fuchsia::feedback::DeviceIdProvider>(
          [&main_service](::fidl::InterfaceRequest<fuchsia::feedback::DeviceIdProvider> request) {
            main_service->HandleDeviceIdProviderRequest(std::move(request));
          }));

  loop.Run();

  return EXIT_SUCCESS;
}
