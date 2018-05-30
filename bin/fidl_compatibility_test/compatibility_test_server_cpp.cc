// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/compatibility/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <zx/channel.h>
#include <cstdlib>
#include <string>

#include "garnet/public/lib/fidl/compatibility_test/echo_client_app.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fidl/cpp/interface_request.h"

namespace fidl {
namespace test {
namespace compatibility {

class EchoServerApp : public Echo {
 public:
  EchoServerApp()
      : context_(component::ApplicationContext::CreateFromStartupInfo()) {
    context_->outgoing().AddPublicService<Echo>(
        [this](fidl::InterfaceRequest<Echo> request) {
          bindings_.AddBinding(
              this, fidl::InterfaceRequest<Echo>(std::move(request)));
        });
  }

  ~EchoServerApp() {}

  void EchoStruct(Struct value, EchoStructCallback callback) override {
    fprintf(stderr, "Server received EchoStruct()\n");
    if (!value.forward_to_server.get().empty()) {
      const std::string forward_to_server = value.forward_to_server;
      value.forward_to_server.reset();  // Prevent recursion.
      EchoClientApp app;
      app.Start(forward_to_server);
      app.echo()->EchoStruct(std::move(value), callback);
      const zx_status_t wait_status = app.echo().WaitForResponse();
      if (wait_status != ZX_OK) {
        fprintf(stderr, "Proxy Got error %d waiting for response from %s\n",
                wait_status, forward_to_server.c_str());
      }
    } else {
      callback(std::move(value));
    }
  }

 private:
  EchoServerApp(const EchoServerApp&) = delete;
  EchoServerApp& operator=(const EchoServerApp&) = delete;

  std::unique_ptr<component::ApplicationContext> context_;
  fidl::BindingSet<Echo> bindings_;
};

}  // namespace compatibility
}  // namespace test
}  // namespace fidl

int main(int argc, const char** argv) {
  // The FIDL support lib requires async_get_default() to return non-null.
  async::Loop loop(&kAsyncLoopConfigMakeDefault);

  fidl::test::compatibility::EchoServerApp app;
  loop.Run();
  return EXIT_SUCCESS;
}
