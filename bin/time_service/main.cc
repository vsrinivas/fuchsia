// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/time_service/time_service.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"

namespace time_service {

class MainService {
 public:
  MainService()
      : app_context_(app::ApplicationContext::CreateFromStartupInfo()) {
    app_context_->outgoing_services()->AddService<TimeService>(
        [this](fidl::InterfaceRequest<TimeService> request) {
          time_svc.AddBinding(std::move(request));
        });
  }

 private:
  std::unique_ptr<app::ApplicationContext> app_context_;
  TimeServiceImpl time_svc;
};

}  // namespace time_service

int main(int argc, char **argv) {
  fsl::MessageLoop loop;
  time_service::MainService svc;
  loop.Run();
  return 0;
}
