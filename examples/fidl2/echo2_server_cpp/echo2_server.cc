// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/fidl/echo2.cc.h>
#include <lib/async/cpp/loop.h>
#include <zx/channel.h>

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
        "echo2.Echo");
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
  async_loop_config_t config = {
      // The FIDL support lib requires async_get_default() to return non-null.
      .make_default_for_current_thread = true,
  };
  async::Loop loop(&config);

  echo2::EchoServerApp app;
  loop.Run();
  return 0;
}
