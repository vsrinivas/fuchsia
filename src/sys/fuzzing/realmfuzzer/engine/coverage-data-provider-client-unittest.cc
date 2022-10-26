// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/realmfuzzer/engine/coverage-data-provider-client.h"

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <zircon/status.h>

#include <memory>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/async-deque.h"
#include "src/sys/fuzzing/common/async-eventpair.h"
#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/testing/async-test.h"
#include "src/sys/fuzzing/realmfuzzer/engine/coverage-data.h"
#include "src/sys/fuzzing/realmfuzzer/testing/module.h"

namespace fuzzing {

using fuchsia::fuzzer::InstrumentedProcess;

// Test fixtures.

class CoverageDataProviderImpl final : public fuchsia::fuzzer::CoverageDataProvider {
 public:
  explicit CoverageDataProviderImpl(ExecutorPtr executor)
      : binding_(this),
        executor_(std::move(executor)),
        options_(MakeOptions()),
        receiver_(&sender_) {}

  ~CoverageDataProviderImpl() = default;

  OptionsPtr options() const { return options_; }

  zx_status_t Bind(zx::channel channel) {
    return binding_.Bind(std::move(channel), executor_->dispatcher());
  }

  void Pend(CoverageData coverage_data) {
    auto status = sender_.Send(std::move(coverage_data));
    FX_CHECK(status == ZX_OK) << zx_status_get_string(status);
  }

  void SetOptions(Options options) override { *options_ = std::move(options); }

  void GetCoverageData(GetCoverageDataCallback callback) override {
    auto task =
        receiver_.Receive()
            .and_then([callback = std::move(callback)](CoverageData& coverage_data) mutable {
              callback(std::move(coverage_data));
              return fpromise::ok();
            })
            .wrap_with(scope_);
    executor_->schedule_task(std::move(task));
  }

  void Unbind() { binding_.Unbind(); }

 private:
  fidl::Binding<CoverageDataProvider> binding_;
  ExecutorPtr executor_;
  OptionsPtr options_;
  AsyncSender<CoverageData> sender_;
  AsyncReceiver<CoverageData> receiver_;
  Scope scope_;
};

class CoverageDataProviderClientTest : public AsyncTest {
 protected:
  void SetUp() override {
    AsyncTest::SetUp();
    provider_ = std::make_unique<CoverageDataProviderImpl>(executor());
  }

  std::unique_ptr<CoverageDataProviderClient> GetProviderClient() {
    auto provider_client = std::make_unique<CoverageDataProviderClient>(executor());
    zx::channel ch1, ch2;
    auto status = zx::channel::create(0, &ch1, &ch2);
    FX_CHECK(status == ZX_OK) << zx_status_get_string(status);
    status = provider_->Bind(std::move(ch1));
    FX_CHECK(status == ZX_OK) << zx_status_get_string(status);
    status = provider_client->Bind(std::move(ch2));
    FX_CHECK(status == ZX_OK) << zx_status_get_string(status);
    return provider_client;
  }

  OptionsPtr GetOptions() const { return provider_->options(); }

  void Pend(CoverageData coverage_data) { provider_->Pend(std::move(coverage_data)); }

 private:
  std::unique_ptr<CoverageDataProviderImpl> provider_;
};

// Unit tests.

TEST_F(CoverageDataProviderClientTest, SetOptions) {
  auto provider_client = GetProviderClient();

  auto options = MakeOptions();
  options->set_runs(3);
  provider_client->Configure(options);
  RunOnce();

  EXPECT_EQ(GetOptions()->runs(), 3U);
}

TEST_F(CoverageDataProviderClientTest, GetProcess) {
  auto provider_client = GetProviderClient();
  CoverageData coverage_data;
  FUZZING_EXPECT_OK(provider_client->GetCoverageData(), &coverage_data);

  auto self = zx::process::self();
  zx_info_handle_basic_t info;
  EXPECT_EQ(self->get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr), ZX_OK);
  auto koid = info.koid;

  zx::process process;
  EXPECT_EQ(self->duplicate(ZX_RIGHT_SAME_RIGHTS, &process), ZX_OK);
  AsyncEventPair eventpair(executor());
  InstrumentedProcess sent{
      .eventpair = eventpair.Create(),
      .process = std::move(process),
  };
  Pend(CoverageData::WithInstrumented(std::move(sent)));
  RunUntilIdle();

  ASSERT_TRUE(coverage_data.is_instrumented());
  auto& received = coverage_data.instrumented();
  EXPECT_EQ(received.process.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr),
            ZX_OK);
  EXPECT_EQ(koid, info.koid);
  FUZZING_EXPECT_OK(eventpair.WaitFor(kSync));
  EXPECT_EQ(received.eventpair.signal_peer(0, kSync), ZX_OK);
  RunUntilIdle();
}

TEST_F(CoverageDataProviderClientTest, GetModule) {
  auto provider_client = GetProviderClient();
  CoverageData coverage_data;

  zx::vmo counters;
  char name[ZX_MAX_NAME_LEN];

  // Send multiple, and verify they arrive in order.
  FakeRealmFuzzerModule module1(1);
  EXPECT_EQ(module1.Share(0x1111, &counters), ZX_OK);
  Pend(CoverageData::WithInline8bitCounters(std::move(counters)));

  FakeRealmFuzzerModule module2(1);
  EXPECT_EQ(module2.Share(0x2222, &counters), ZX_OK);
  Pend(CoverageData::WithInline8bitCounters(std::move(counters)));

  FUZZING_EXPECT_OK(provider_client->GetCoverageData(), &coverage_data);
  RunUntilIdle();
  ASSERT_TRUE(coverage_data.is_inline_8bit_counters());
  auto& counters1 = coverage_data.inline_8bit_counters();
  EXPECT_EQ(counters1.get_property(ZX_PROP_NAME, name, sizeof(name)), ZX_OK);
  EXPECT_EQ(GetTargetId(name), 0x1111U);
  EXPECT_EQ(GetModuleId(name), module1.id());

  FUZZING_EXPECT_OK(provider_client->GetCoverageData(), &coverage_data);
  RunUntilIdle();
  ASSERT_TRUE(coverage_data.is_inline_8bit_counters());
  auto& counters2 = coverage_data.inline_8bit_counters();
  EXPECT_EQ(counters2.get_property(ZX_PROP_NAME, name, sizeof(name)), ZX_OK);
  EXPECT_EQ(GetTargetId(name), 0x2222U);
  EXPECT_EQ(GetModuleId(name), module2.id());

  // Intentionally drop a |GetCoverageData| future and ensure no data is lost.
  FakeRealmFuzzerModule module3(3);
  {
    auto dropped = provider_client->GetCoverageData();
    RunOnce();
    EXPECT_EQ(module3.Share(0x1111, &counters), ZX_OK);
    Pend(CoverageData::WithInline8bitCounters(std::move(counters)));
  }

  FUZZING_EXPECT_OK(provider_client->GetCoverageData(), &coverage_data);
  RunUntilIdle();
  ASSERT_TRUE(coverage_data.is_inline_8bit_counters());
  auto& counters3 = coverage_data.inline_8bit_counters();
  EXPECT_EQ(counters3.get_property(ZX_PROP_NAME, name, sizeof(name)), ZX_OK);
  EXPECT_EQ(GetTargetId(name), 0x1111U);
  EXPECT_EQ(GetModuleId(name), module3.id());
}

}  // namespace fuzzing
