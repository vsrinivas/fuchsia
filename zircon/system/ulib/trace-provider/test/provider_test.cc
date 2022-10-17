// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.tracing.provider/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/trace-provider/provider.h>

#include <zxtest/zxtest.h>

namespace trace {
namespace {

class FakeTraceManager : public fidl::WireServer<fuchsia_tracing_provider::Registry> {
 public:
  FakeTraceManager(async_dispatcher_t* dispatcher,
                   fidl::ServerEnd<fuchsia_tracing_provider::Registry> server_end) {
    fidl::BindServer(dispatcher, std::move(server_end), this,
                     [](FakeTraceManager* impl, fidl::UnbindInfo info,
                        fidl::ServerEnd<fuchsia_tracing_provider::Registry> server_end) {
                       fprintf(stderr, "FakeTraceManager: FIDL server unbound: info=%s\n",
                               info.FormatDescription().c_str());
                     });
  }

  void RegisterProvider(fuchsia_tracing_provider::wire::RegistryRegisterProviderRequest* request,
                        RegisterProviderCompleter::Sync& completer) override {
    printf("FakeTraceManager: Got RegisterProvider request\n");
  }

  void RegisterProviderSynchronously(
      fuchsia_tracing_provider::wire::RegistryRegisterProviderSynchronouslyRequest* request,
      RegisterProviderSynchronouslyCompleter::Sync& completer) override {}
};

// Test handling of early loop cancel by having the loop be destructed before the provider.
TEST(ProviderTest, EarlyLoopCancel) {
  zx::result endpoints = fidl::CreateEndpoints<fuchsia_tracing_provider::Registry>();
  ASSERT_TRUE(endpoints.is_ok(), "%s", endpoints.status_string());

  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};

  const FakeTraceManager manager{loop.dispatcher(), std::move(endpoints->server)};
  const TraceProvider provider{endpoints->client.TakeChannel(), loop.dispatcher()};

  loop.RunUntilIdle();
  loop.Shutdown();
}

}  // namespace
}  // namespace trace
