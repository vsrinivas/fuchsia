// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "apps/modular/lib/app/application_context.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

class HelloComponentApp {
 public:
  HelloComponentApp()
      : context_(modular::ApplicationContext::CreateFromStartupInfo()) {
    FTL_LOG(INFO) << "HelloComponentApp::OnInitialize()";
  }

 private:
  std::unique_ptr<modular::ApplicationContext> context_;
  FTL_DISALLOW_COPY_AND_ASSIGN(HelloComponentApp);
};

}  // namespace

int main(int argc, const char** argv) {
  FTL_LOG(INFO) << "hello_component main";
  mtl::MessageLoop loop;
  HelloComponentApp hello_component_app;
  loop.Run();
  return 0;
}
