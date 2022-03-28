// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/string.h>
#include <lib/sys/cpp/component_context.h>

#include <iostream>
#include <sstream>
#include <string>
#include <utility>

#include <echo_server_config_lib/config.h>
#include <test/placeholders/cpp/fidl.h>

class EchoServer : public test::placeholders::Echo {
 public:
  explicit EchoServer(echo_server_config_lib::Config c) : config(std::move(c)) {}

  void EchoString(::fidl::StringPtr value, EchoStringCallback callback) override {
    std::string intercept = value.value_or("");

    std::string return_value =
        intercept + " [" + std::to_string(config.my_flag) + "][" + std::to_string(config.my_uint8) +
        "][" + std::to_string(config.my_uint16) + "][" + std::to_string(config.my_uint32) + "][" +
        std::to_string(config.my_uint64) + "][" + std::to_string(config.my_int8) + "][" +
        std::to_string(config.my_int16) + "][" + std::to_string(config.my_int32) + "][" +
        std::to_string(config.my_int64) + "][" + config.my_string + "][";
    for (const auto& value : config.my_vector_of_flag) {
      return_value += std::to_string(value) + ",";
    }
    return_value += "][";
    for (const auto& value : config.my_vector_of_uint8) {
      return_value += std::to_string(value) + ",";
    }
    return_value += "][";
    for (const auto& value : config.my_vector_of_uint16) {
      return_value += std::to_string(value) + ",";
    }
    return_value += "][";
    for (const auto& value : config.my_vector_of_uint32) {
      return_value += std::to_string(value) + ",";
    }
    return_value += "][";
    for (const auto& value : config.my_vector_of_uint64) {
      return_value += std::to_string(value) + ",";
    }
    return_value += "][";
    for (const auto& value : config.my_vector_of_int8) {
      return_value += std::to_string(value) + ",";
    }
    return_value += "][";
    for (const auto& value : config.my_vector_of_int16) {
      return_value += std::to_string(value) + ",";
    }
    return_value += "][";
    for (const auto& value : config.my_vector_of_int32) {
      return_value += std::to_string(value) + ",";
    }
    return_value += "][";
    for (const auto& value : config.my_vector_of_int64) {
      return_value += std::to_string(value) + ",";
    }
    return_value += "][";
    for (const auto& value : config.my_vector_of_string) {
      return_value += value + ",";
    }
    return_value += "]";

    ZX_ASSERT(return_value.size() < 4096);

    callback(std::move(return_value));
    if (listener_) {
      listener_(std::move(intercept));
    }
  }

  fidl::InterfaceRequestHandler<test::placeholders::Echo> GetHandler() {
    return bindings_.GetHandler(this);
  }

  void SetListener(fit::function<void(std::string)> list) { listener_ = std::move(list); }

 private:
  fidl::BindingSet<test::placeholders::Echo> bindings_;
  fit::function<void(std::string)> listener_;
  echo_server_config_lib::Config config;
};

int main(int argc, const char** argv) {
  auto c = echo_server_config_lib::Config::from_args();
  std::cout << "Starting echo server." << std::endl;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto startup = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  std::unique_ptr<EchoServer> echo_server = std::make_unique<EchoServer>(std::move(c));
  startup->outgoing()->AddPublicService(echo_server->GetHandler());
  loop.Run();

  std::cout << "Stopping echo server." << std::endl;
  return 0;
}
