// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>

#include "garnet/bin/timezone/timezone.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fxl/logging.h"

namespace time_zone {

class MainService {
 public:
  MainService()
      : app_context_(component::ApplicationContext::CreateFromStartupInfo()) {
    app_context_->outgoing().AddPublicService<Timezone>(
        [this](fidl::InterfaceRequest<Timezone> request) {
          timezone_.AddBinding(std::move(request));
        });
  }

 private:
  std::unique_ptr<component::ApplicationContext> app_context_;
  TimezoneImpl timezone_;
};

}  // namespace time_zone

int main(int argc, char** argv) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  time_zone::MainService svc;
  loop.Run();
  return 0;
}
