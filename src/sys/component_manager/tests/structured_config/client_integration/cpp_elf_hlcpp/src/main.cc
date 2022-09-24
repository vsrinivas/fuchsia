// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/inspect/cpp/component.h>
#include <lib/syslog/cpp/macros.h>

#include <iostream>

#include <test/structuredconfig/receiver/cpp/fidl.h>

#include "lib/inspect/cpp/vmo/types.h"
#include "src/sys/component_manager/tests/structured_config/client_integration/cpp_elf_hlcpp/receiver_config.h"

class PuppetImpl : public test::structuredconfig::receiver::ConfigReceiverPuppet {
 public:
  explicit PuppetImpl(const receiver_config::Config& c) {
    this->c.my_flag = c.my_flag();
    this->c.my_int8 = c.my_int8();
    this->c.my_int16 = c.my_int16();
    this->c.my_int32 = c.my_int32();
    this->c.my_int64 = c.my_int64();
    this->c.my_uint8 = c.my_uint8();
    this->c.my_uint16 = c.my_uint16();
    this->c.my_uint32 = c.my_uint32();
    this->c.my_uint64 = c.my_uint64();
    this->c.my_string = c.my_string();
    this->c.my_vector_of_flag = c.my_vector_of_flag();
    this->c.my_vector_of_int8 = c.my_vector_of_int8();
    this->c.my_vector_of_int16 = c.my_vector_of_int16();
    this->c.my_vector_of_int32 = c.my_vector_of_int32();
    this->c.my_vector_of_int64 = c.my_vector_of_int64();
    this->c.my_vector_of_uint8 = c.my_vector_of_uint8();
    this->c.my_vector_of_uint16 = c.my_vector_of_uint16();
    this->c.my_vector_of_uint32 = c.my_vector_of_uint32();
    this->c.my_vector_of_uint64 = c.my_vector_of_uint64();
    this->c.my_vector_of_string = c.my_vector_of_string();
  }

  void GetConfig(GetConfigCallback callback) override { callback(this->c); }

  test::structuredconfig::receiver::ReceiverConfig c;
};

int main(int argc, const char** argv) {
  auto c = receiver_config::Config::TakeFromStartupHandle();
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  sys::ComponentInspector inspector(context.get());
  inspect::Node inspect_config = inspector.root().CreateChild("config");
  c.RecordInspect(&inspect_config);

  PuppetImpl impl(c);
  fidl::Binding<test::structuredconfig::receiver::ConfigReceiverPuppet> binding(&impl);
  fidl::InterfaceRequestHandler<test::structuredconfig::receiver::ConfigReceiverPuppet> handler =
      [&](fidl::InterfaceRequest<test::structuredconfig::receiver::ConfigReceiverPuppet> request) {
        binding.Bind(std::move(request));
      };
  context->outgoing()->AddPublicService(std::move(handler));

  return loop.Run();
}
