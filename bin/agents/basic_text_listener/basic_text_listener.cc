// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "application/lib/app/application_context.h"
#include "apps/maxwell/services/context/context_provider.fidl.h"
#include "lib/mtl/tasks/message_loop.h"

namespace maxwell {

class BasicTextListener : ContextListener {
 public:
  BasicTextListener()
      : app_context_(app::ApplicationContext::CreateFromStartupInfo()),
        provider_(
            app_context_->ConnectToEnvironmentService<ContextProvider>()),
        binding_(this) {
    FTL_LOG(INFO) << "[Basic Text Listener] " << "Initializing";
    auto query = ContextQuery::New();
    provider_->Subscribe(std::move(query), binding_.NewBinding());
  }

 private:
  // |ContextListener|
  void OnUpdate(ContextUpdatePtr result) override {
    const auto& values = result.get()->values;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
      FTL_LOG(INFO) << "[Basic Text Listener] " << it.GetKey() << ":" << it.GetValue();
      std::cout << it.GetKey() << ":" << it.GetValue();
    }
  }
  std::unique_ptr<app::ApplicationContext> app_context_;

  ContextProviderPtr provider_;
  fidl::Binding<ContextListener> binding_;
};

}  // namespace maxwell

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  maxwell::BasicTextListener app;
  loop.Run();
  return 0;
}
