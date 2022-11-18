// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.tracing.provider/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/trace-provider/provider.h>

#include <memory>
#include <sstream>
#include <string>
#include <utility>

#include <zxtest/zxtest.h>

#include "fidl/fuchsia.tracing.provider/cpp/common_types.h"
#include "fidl/fuchsia.tracing.provider/cpp/markers.h"
#include "fidl/fuchsia.tracing.provider/cpp/natural_types.h"
#include "lib/fidl/cpp/channel.h"
#include "lib/fidl/cpp/wire/channel.h"
#include "lib/fit/internal/result.h"
#include "lib/trace-engine/types.h"
#include "zxtest/base/parameterized-value.h"
#include "zxtest/base/values.h"

namespace trace {
namespace {

class FakeTraceManager : public fidl::WireServer<fuchsia_tracing_provider::Registry> {
 public:
  FakeTraceManager(async_dispatcher_t* dispatcher,
                   fidl::ServerEnd<fuchsia_tracing_provider::Registry> server_end,
                   std::vector<std::string> categories,
                   fuchsia_tracing_provider::BufferingMode buffering_mode)
      : categories_(std::move(categories)), buffering_mode_(buffering_mode) {
    fidl::BindServer(dispatcher, std::move(server_end), this,
                     [](FakeTraceManager* impl, fidl::UnbindInfo info,
                        fidl::ServerEnd<fuchsia_tracing_provider::Registry> server_end) {
                       fprintf(stderr, "FakeTraceManager: FIDL server unbound: info=%s\n",
                               info.FormatDescription().c_str());
                     });
  }

  void RegisterProvider(fuchsia_tracing_provider::wire::RegistryRegisterProviderRequest* request,
                        RegisterProviderCompleter::Sync& completer) override {
    fidl::SyncClient client(std::move(request->provider));

    zx::vmo buffer_vmo;
    ASSERT_EQ(zx::vmo::create(42, 0u, &buffer_vmo), ZX_OK);

    zx::fifo fifo, fifo_for_provider;
    ASSERT_EQ(zx::fifo::create(42, sizeof(trace_provider_packet_t), 0u, &fifo, &fifo_for_provider),
              ZX_OK);

    fuchsia_tracing_provider::ProviderConfig config({
        .buffering_mode = buffering_mode_,
        .buffer = std::move(buffer_vmo),
        .fifo = std::move(fifo_for_provider),
        .categories = categories_,
    });
    fit::result result = client->Initialize({std::move(config)});
    ASSERT_TRUE(result.is_ok(), "%s error calling Initialize: %s", request->name.data(),
                result.error_value().status_string());
  }

  void RegisterProviderSynchronously(
      fuchsia_tracing_provider::wire::RegistryRegisterProviderSynchronouslyRequest* request,
      RegisterProviderSynchronouslyCompleter::Sync& completer) override {}

 private:
  const std::vector<std::string> categories_;
  const fuchsia_tracing_provider::BufferingMode buffering_mode_;
};

struct TestParams {
  std::vector<std::string> categories;
  fuchsia_tracing_provider::BufferingMode buffering_mode;
  ProviderConfig expected_config;
};

class ParameterizedProviderTest : public zxtest::TestWithParam<TestParams> {
 public:
  ParameterizedProviderTest()
      : endpoints_(fidl::CreateEndpoints<fuchsia_tracing_provider::Registry>()) {
    ASSERT_TRUE(endpoints_.is_ok(), "%s", endpoints_.status_string());

    manager_ = std::make_unique<FakeTraceManager>(loop_.dispatcher(), std::move(endpoints_->server),
                                                  GetParam().categories, GetParam().buffering_mode);
    provider_ =
        std::make_unique<TraceProvider>(endpoints_->client.TakeChannel(), loop_.dispatcher());
  }

  void TearDown() override {
    loop_.Shutdown();
    // `provider_` must be deleted before `manager_` to avoid use after free. Changing declaration
    // order is insufficient.
    provider_.reset();
    manager_.reset();
  }

  std::unique_ptr<FakeTraceManager> manager_;
  std::unique_ptr<TraceProvider> provider_;
  async::Loop loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
  zx::result<fidl::Endpoints<fuchsia_tracing_provider::Registry>> endpoints_;
};

// Test handling of early loop cancel by having the loop be destructed before the provider.
TEST_P(ParameterizedProviderTest, EarlyLoopCancel) { loop_.RunUntilIdle(); }

// Test that the provider config sent to the provider on initialization is made available via
// GetProviderConfig.
TEST_P(ParameterizedProviderTest, GetProviderConfig) {
  loop_.RunUntilIdle();

  EXPECT_EQ(GetParam().expected_config.categories, provider_->GetProviderConfig().categories);
  EXPECT_EQ(GetParam().expected_config.buffering_mode,
            provider_->GetProviderConfig().buffering_mode);
}

INSTANTIATE_TEST_SUITE_P(
    ProviderTest, ParameterizedProviderTest,
    zxtest::Values(TestParams{.categories = {"expirationsun", "crossfoil"},
                              .buffering_mode = fuchsia_tracing_provider::BufferingMode::kOneshot,
                              .expected_config =
                                  {
                                      .buffering_mode = TRACE_BUFFERING_MODE_ONESHOT,
                                      .categories = {"expirationsun", "crossfoil"},
                                  }},
                   TestParams{.buffering_mode = fuchsia_tracing_provider::BufferingMode::kCircular,
                              .expected_config =
                                  {
                                      .buffering_mode = TRACE_BUFFERING_MODE_CIRCULAR,
                                  }},
                   TestParams{.buffering_mode = fuchsia_tracing_provider::BufferingMode::kStreaming,
                              .expected_config = {
                                  .buffering_mode = TRACE_BUFFERING_MODE_STREAMING,
                              }}));

}  // namespace
}  // namespace trace
