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

#include <receiver_config/config.h>
#include <test/structuredconfig/receiver/cpp/fidl.h>

#include "lib/inspect/cpp/vmo/types.h"

class PuppetImpl : public test::structuredconfig::receiver::ConfigReceiverPuppet {
 public:
  explicit PuppetImpl(const receiver_config::Config& c) {
    this->c.my_flag = c.my_flag;
    this->c.my_int8 = c.my_int8;
    this->c.my_int16 = c.my_int16;
    this->c.my_int32 = c.my_int32;
    this->c.my_int64 = c.my_int64;
    this->c.my_uint8 = c.my_uint8;
    this->c.my_uint16 = c.my_uint16;
    this->c.my_uint32 = c.my_uint32;
    this->c.my_uint64 = c.my_uint64;
    this->c.my_string = c.my_string;
    this->c.my_vector_of_flag = c.my_vector_of_flag;
    this->c.my_vector_of_int8 = c.my_vector_of_int8;
    this->c.my_vector_of_int16 = c.my_vector_of_int16;
    this->c.my_vector_of_int32 = c.my_vector_of_int32;
    this->c.my_vector_of_int64 = c.my_vector_of_int64;
    this->c.my_vector_of_uint8 = c.my_vector_of_uint8;
    this->c.my_vector_of_uint16 = c.my_vector_of_uint16;
    this->c.my_vector_of_uint32 = c.my_vector_of_uint32;
    this->c.my_vector_of_uint64 = c.my_vector_of_uint64;
    this->c.my_vector_of_string = c.my_vector_of_string;
  }

  void GetConfig(GetConfigCallback callback) override { callback(this->c); }

  test::structuredconfig::receiver::ReceiverConfig c;
};

int main(int argc, const char** argv) {
  auto c = receiver_config::Config::from_args();
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  // TODO(http://fxbug.dev/92897): Add a `record_to_inspect` method in the config client library.
  sys::ComponentInspector inspector(context.get());
  auto inspect_config = inspector.root().CreateChild("config");
  inspect_config.CreateBool("my_flag", c.my_flag, &inspector);
  inspect_config.CreateInt("my_int8", c.my_int8, &inspector);
  inspect_config.CreateInt("my_int16", c.my_int16, &inspector);
  inspect_config.CreateInt("my_int32", c.my_int32, &inspector);
  inspect_config.CreateInt("my_int64", c.my_int64, &inspector);
  inspect_config.CreateUint("my_uint8", c.my_uint8, &inspector);
  inspect_config.CreateUint("my_uint16", c.my_uint16, &inspector);
  inspect_config.CreateUint("my_uint32", c.my_uint32, &inspector);
  inspect_config.CreateUint("my_uint64", c.my_uint64, &inspector);
  inspect_config.CreateString("my_string", c.my_string, &inspector);

  auto my_vector_of_flag =
      inspect_config.CreateIntArray("my_vector_of_flag", c.my_vector_of_flag.size());
  for (size_t i = 0; i < c.my_vector_of_flag.size(); i++) {
    my_vector_of_flag.Set(i, c.my_vector_of_flag[i]);
  }

  auto my_vector_of_int8 =
      inspect_config.CreateIntArray("my_vector_of_int8", c.my_vector_of_int8.size());
  for (size_t i = 0; i < c.my_vector_of_int8.size(); i++) {
    my_vector_of_int8.Set(i, c.my_vector_of_int8[i]);
  }

  auto my_vector_of_int16 =
      inspect_config.CreateIntArray("my_vector_of_int16", c.my_vector_of_int16.size());
  for (size_t i = 0; i < c.my_vector_of_int16.size(); i++) {
    my_vector_of_int16.Set(i, c.my_vector_of_int16[i]);
  }

  auto my_vector_of_int32 =
      inspect_config.CreateIntArray("my_vector_of_int32", c.my_vector_of_int32.size());
  for (size_t i = 0; i < c.my_vector_of_int32.size(); i++) {
    my_vector_of_int32.Set(i, c.my_vector_of_int32[i]);
  }

  auto my_vector_of_int64 =
      inspect_config.CreateIntArray("my_vector_of_int64", c.my_vector_of_int64.size());
  for (size_t i = 0; i < c.my_vector_of_int64.size(); i++) {
    my_vector_of_int64.Set(i, c.my_vector_of_int64[i]);
  }

  auto my_vector_of_uint8 =
      inspect_config.CreateUintArray("my_vector_of_uint8", c.my_vector_of_uint8.size());
  for (size_t i = 0; i < c.my_vector_of_uint8.size(); i++) {
    my_vector_of_uint8.Set(i, c.my_vector_of_uint8[i]);
  }

  auto my_vector_of_uint16 =
      inspect_config.CreateUintArray("my_vector_of_uint16", c.my_vector_of_uint16.size());
  for (size_t i = 0; i < c.my_vector_of_uint16.size(); i++) {
    my_vector_of_uint16.Set(i, c.my_vector_of_uint16[i]);
  }

  auto my_vector_of_uint32 =
      inspect_config.CreateUintArray("my_vector_of_uint32", c.my_vector_of_uint32.size());
  for (size_t i = 0; i < c.my_vector_of_uint32.size(); i++) {
    my_vector_of_uint32.Set(i, c.my_vector_of_uint32[i]);
  }

  auto my_vector_of_uint64 =
      inspect_config.CreateUintArray("my_vector_of_uint64", c.my_vector_of_uint64.size());
  for (size_t i = 0; i < c.my_vector_of_uint64.size(); i++) {
    my_vector_of_uint64.Set(i, c.my_vector_of_uint64[i]);
  }

  auto my_vector_of_string =
      inspect_config.CreateStringArray("my_vector_of_string", c.my_vector_of_string.size());
  for (size_t i = 0; i < c.my_vector_of_string.size(); i++) {
    auto ref = std::string_view(c.my_vector_of_string[i].data());
    my_vector_of_string.Set(i, ref);
  }

  PuppetImpl impl(c);
  fidl::Binding<test::structuredconfig::receiver::ConfigReceiverPuppet> binding(&impl);
  fidl::InterfaceRequestHandler<test::structuredconfig::receiver::ConfigReceiverPuppet> handler =
      [&](fidl::InterfaceRequest<test::structuredconfig::receiver::ConfigReceiverPuppet> request) {
        binding.Bind(std::move(request));
      };
  context->outgoing()->AddPublicService(std::move(handler));

  return loop.Run();
}
