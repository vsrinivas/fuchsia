// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "lib/app/cpp/application_context.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

namespace {

class HelloComponentApp {
 public:
  HelloComponentApp()
      : context_(app::ApplicationContext::CreateFromStartupInfo()) {
    FXL_LOG(INFO) << "HelloComponentApp::OnInitialize()";
  }

 private:
  std::unique_ptr<app::ApplicationContext> context_;
  FXL_DISALLOW_COPY_AND_ASSIGN(HelloComponentApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  FXL_LOG(INFO) << "hello_component main";
  fsl::MessageLoop loop;
  HelloComponentApp hello_component_app;
  loop.Run();
  return 0;
}
