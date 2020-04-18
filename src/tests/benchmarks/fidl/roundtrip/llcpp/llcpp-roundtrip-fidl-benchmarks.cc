// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/sys/cpp/component_context.h>

#include <vector>

#include <fidl/benchmarks/llcpp/fidl.h>

using llcpp::fidl::benchmarks::BindingsUnderTest;

class UnderTest final : public BindingsUnderTest::Interface {
 public:
  explicit UnderTest(zx::unowned_channel channel) : channel_(channel) {}

  void EchoBytes(::fidl::VectorView<uint8_t> input, EchoBytesCompleter::Sync completer) override {
    completer.Reply(std::move(input));
  }
  void EchoString(::fidl::StringView input, EchoStringCompleter::Sync completer) override {
    completer.Reply(std::move(input));
  }
  void EchoStrings(::fidl::VectorView<::fidl::StringView> input,
                   EchoStringsCompleter::Sync completer) override {
    completer.Reply(std::move(input));
  }
  void EchoHandles(::fidl::VectorView<::zx::handle> input,
                   EchoHandlesCompleter::Sync completer) override {
    completer.Reply(std::move(input));
  }

 private:
  zx::unowned_channel channel_;
};

int main(int argc, const char** argv) {
  // The FIDL support lib requires async_get_default_dispatcher() to return
  // non-null.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::Create();
  std::vector<std::unique_ptr<BindingsUnderTest::Interface>> connections;

  context->outgoing()->AddPublicService(
      std::make_unique<vfs::Service>([&](zx::channel request, async_dispatcher_t* dispatcher) {
        auto conn = std::make_unique<UnderTest>(zx::unowned_channel(request));
        ZX_ASSERT(::fidl::Bind(dispatcher, std::move(request), conn.get()) == ZX_OK);
        connections.push_back(std::move(conn));
      }),
      llcpp::fidl::benchmarks::BindingsUnderTest::Name);

  loop.Run();
  return EXIT_SUCCESS;
}
