// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/trace_provider_registry_impl.h"

#include <thread>

#include "gtest/gtest.h"
#include "lib/mtl/data_pipe/strings.h"
#include "mojo/public/cpp/application/application_test_base.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/utility/run_loop.h"

using TraceProviderRegistryImplTest = mojo::test::ApplicationTestBase;

namespace tracing {
namespace {

class TestTraceProvider : public TraceProvider {
 public:
  explicit TestTraceProvider(const mojo::String& json,
                             const mojo::String& categories)
      : expected_categories_(categories), json_(json) {}

  void StartTracing(const mojo::String& categories,
                    mojo::InterfaceHandle<TraceRecorder> recorder) override {
    tracing_started_ = categories == expected_categories_;
    trace_recorder_ = TraceRecorderPtr::Create(std::move(recorder));
  }

  void StopTracing() override {
    tracing_stopped_ = true;
    trace_recorder_->Record(json_);
  }

  bool tracing_started() const { return tracing_started_; }
  bool tracing_stopped() const { return tracing_stopped_; }

 private:
  mojo::String expected_categories_;
  mojo::String json_;
  bool tracing_started_ = false;
  bool tracing_stopped_ = false;
  TraceRecorderPtr trace_recorder_;
};

// This test exercises TraceProviderRegistryImpl end to end, making sure that:
//   - providers are registered correctly
//   - registered providers are started and stopped correctly
//   - trace data is collected and sent out correctly
// Two trace providers are registered with the registry. Trace collection is
// started and stopped and final results are collected.
TEST_F(TraceProviderRegistryImplTest, ProviderRegistrationAndTraceCollection) {
  const mojo::String expected_categories("one,two,three,four");

  TraceCollectorPtr collector_ptr;
  TraceProviderRegistryPtr registry_ptr;
  TraceProviderRegistryImpl registry_impl(std::chrono::seconds(1));

  mojo::Binding<TraceProviderRegistry> trace_recorder_binding(
      &registry_impl, mojo::GetProxy(&registry_ptr));
  mojo::Binding<TraceCollector> trace_collector_binding(
      &registry_impl, mojo::GetProxy(&collector_ptr));

  TestTraceProvider tp1("{\"key1\": \"value1\"}", expected_categories);
  TestTraceProvider tp2("{\"key2\": \"value2\"}", expected_categories);

  mojo::InterfaceHandle<TraceProvider> tpi1, tpi2;

  mojo::Binding<TraceProvider> tpb1(&tp1, mojo::GetProxy(&tpi1));
  mojo::Binding<TraceProvider> tpb2(&tp2, mojo::GetProxy(&tpi2));

  registry_ptr->RegisterTraceProvider(std::move(tpi1));
  registry_ptr->RegisterTraceProvider(std::move(tpi2));

  // After this call completes, provider registration should be finished.
  mojo::RunLoop::current()->RunUntilIdle();

  mojo::DataPipe data_pipe;
  collector_ptr->Start(std::move(data_pipe.producer_handle),
                       expected_categories);
  mojo::RunLoop::current()->RunUntilIdle();

  collector_ptr->StopAndFlush();
  mojo::RunLoop::current()->RunUntilIdle();

  // We have to wait for the registry to pass its grace period
  // and spin the current RunLoop a last time to make sure
  // that the producer_handle of the data pipe is correctly closed.
  std::this_thread::sleep_for(std::chrono::seconds(2));
  mojo::RunLoop::current()->RunUntilIdle();

  EXPECT_TRUE(tp1.tracing_started());
  EXPECT_TRUE(tp1.tracing_stopped());
  EXPECT_TRUE(tp2.tracing_started());
  EXPECT_TRUE(tp2.tracing_stopped());

  std::string result;
  mtl::BlockingCopyToString(std::move(data_pipe.consumer_handle), &result);

  EXPECT_EQ("[{\"key1\": \"value1\"},{\"key2\": \"value2\"}]", result);
}

}  // namespace
}  // namespace tracing
