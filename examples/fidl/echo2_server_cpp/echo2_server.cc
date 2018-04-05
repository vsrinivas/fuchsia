// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/cpp/echo2.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/zx/channel.h>

#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fidl/cpp/interface_request.h"

namespace echo2 {

class EchoServerApp : public Echo {
 public:
  EchoServerApp()
      : context_(component::ApplicationContext::CreateFromStartupInfo()) {
    context_->outgoing_services()->AddServiceForName(
        [this](zx::channel request) {
          bindings_.AddBinding(
              this, fidl::InterfaceRequest<Echo>(std::move(request)));
        },
        Echo::Name_);
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
  // The FIDL support lib requires async_get_default() to return non-null.
  async::Loop loop(&kAsyncLoopConfigMakeDefault);

  echo2::EchoServerApp app;
  loop.Run();
  return 0;
}
