// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/compatibility/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/channel.h>
#include <src/lib/fxl/logging.h>
#include <zircon/types.h>
#include <cstdlib>
#include <string>

#include "garnet/public/lib/fidl/compatibility_test/echo_client_app.h"

namespace fidl {
namespace test {
namespace compatibility {

class EchoServerApp : public Echo {
 public:
  explicit EchoServerApp(async::Loop* loop)
      : loop_(loop), context_(sys::ComponentContext::Create()) {
    context_->outgoing()->AddPublicService(bindings_.GetHandler(this));
  }

  ~EchoServerApp() {}

  void EchoStruct(Struct value, std::string forward_to_server,
                  EchoStructCallback callback) override {
    if (!forward_to_server.empty()) {
      EchoClientApp app;
      bool failed = false;
      app.echo().set_error_handler([this, &forward_to_server, &failed](zx_status_t status) {
        failed = true;
        loop_->Quit();
        FXL_LOG(ERROR) << "error communicating with " << forward_to_server << ": " << status;
      });
      app.Start(forward_to_server);
      bool called_back = false;
      app.echo()->EchoStruct(std::move(value), "",
                             [this, &called_back, &callback](Struct resp) {
                               called_back = true;
                               callback(std::move(resp));
                               loop_->Quit();
                             });
      while (!called_back && !failed) {
        loop_->Run();
      }
      loop_->ResetQuit();
    } else {
      callback(std::move(value));
    }
  }

  void EchoStructNoRetVal(Struct value,
                          std::string forward_to_server) override {
    if (!forward_to_server.empty()) {
      std::unique_ptr<EchoClientApp> app(new EchoClientApp);
      app->echo().set_error_handler([this, &forward_to_server](zx_status_t status) {
        loop_->Quit();
        FXL_LOG(ERROR) << "error communicating with " << forward_to_server << ": " << status;
      });
      app->Start(forward_to_server);
      app->echo().events().EchoEvent = [this](Struct resp) {
        this->HandleEchoEvent(std::move(resp));
      };
      app->echo()->EchoStructNoRetVal(std::move(value), "");
      client_apps_.push_back(std::move(app));
    } else {
      for (const auto& binding : bindings_.bindings()) {
        Struct to_send;
        value.Clone(&to_send);
        binding->events().EchoEvent(std::move(to_send));
      }
    }
  }

 private:
  void HandleEchoEvent(Struct value) {
    for (const auto& binding : bindings_.bindings()) {
      Struct to_send;
      value.Clone(&to_send);
      binding->events().EchoEvent(std::move(to_send));
    }
  }

  EchoPtr server_ptr;
  EchoServerApp(const EchoServerApp&) = delete;
  EchoServerApp& operator=(const EchoServerApp&) = delete;

  async::Loop* loop_;
  std::unique_ptr<sys::ComponentContext> context_;
  fidl::BindingSet<Echo> bindings_;
  std::vector<std::unique_ptr<EchoClientApp>> client_apps_;
};

}  // namespace compatibility
}  // namespace test
}  // namespace fidl

int main(int argc, const char** argv) {
  // The FIDL support lib requires async_get_default_dispatcher() to return
  // non-null.
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  fidl::test::compatibility::EchoServerApp app(&loop);
  loop.Run();
  return EXIT_SUCCESS;
}
