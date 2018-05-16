// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <echo2/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/zx/channel.h>

#include "lib/app/cpp/application_context.h"

namespace echo2 {

class EchoServerApp : public Echo {
 public:
  EchoServerApp()
      : context_(component::ApplicationContext::CreateFromStartupInfo()) {
    context_->outgoing().AddPublicService<Echo>(
        [this](fidl::InterfaceRequest<Echo> request) {
          bindings_.AddBinding(this, std::move(request));
        });
  }

  ~EchoServerApp() {}

  void EchoString(fidl::StringPtr value, EchoStringCallback callback) override {
    printf("EchoString: %s\n", value->data());
    callback(std::move(value));
  }

 private:
  EchoServerApp(const EchoServerApp&) = delete;
  EchoServerApp& operator=(const EchoServerApp&) = delete;

  std::unique_ptr<component::ApplicationContext> context_;
  fidl::BindingSet<Echo> bindings_;
};

}  // namespace echo2

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);

  echo2::EchoServerApp app;
  loop.Run();
  return 0;
}
