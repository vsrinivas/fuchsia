// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <utility>

#include "lib/async-loop/cpp/loop.h"
#include "lib/async-loop/default.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fidl/cpp/string.h"
#include "lib/sys/cpp/component_context.h"
#include "src/lib/fxl/command_line.h"
#include "test/placeholders/cpp/fidl.h"

class EchoServer : public test::placeholders::Echo {
 public:
  explicit EchoServer(std::string default_reply) : default_reply_(std::move(default_reply)) {}

  void EchoString(::fidl::StringPtr value, EchoStringCallback callback) override {
    std::string reply = value.value_or(default_reply_);
    callback(fidl::StringPtr(reply));
    if (listener_) {
      listener_(std::move(reply));
    }
  }

  fidl::InterfaceRequestHandler<test::placeholders::Echo> GetHandler() {
    return bindings_.GetHandler(this);
  }

  void SetListener(fit::function<void(std::string)> list) { listener_ = std::move(list); }

 private:
  std::string default_reply_;

  fidl::BindingSet<test::placeholders::Echo> bindings_;
  fit::function<void(std::string)> listener_;
};

int main(int argc, const char** argv) {
  std::cout << "Starting echo server." << std::endl;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  std::string default_reply = "UNSET";
  if (argc > 1) {
    default_reply = argv[1];
  }
  std::cout << "Default reply set to '" << default_reply << "'" << std::endl;
  std::unique_ptr<EchoServer> echo_server = std::make_unique<EchoServer>(default_reply);

  auto startup = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  startup->outgoing()->AddPublicService(echo_server->GetHandler());
  loop.Run();

  std::cout << "Stopping echo server." << std::endl;
  return 0;
}
