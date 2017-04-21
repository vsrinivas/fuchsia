// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/trace_event/tracing_client.h"

#include <iostream>
#include <sstream>
#include <thread>
#include <unordered_map>

#include "mojo/public/cpp/application/application_test_base.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/bindings/binding_set.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "mojo/public/cpp/system/macros.h"
#include "mojo/public/cpp/utility/run_loop.h"

#include "gtest/gtest.h"
#include "lib/trace_event/trace_event.h"
#include "mojo/services/tracing/interfaces/trace_provider_registry.mojom.h"
#include "mojo/services/tracing/interfaces/tracing.mojom.h"

#include "rapidjson/document.h"
#include "rapidjson/ostreamwrapper.h"
#include "rapidjson/writer.h"

namespace json = rapidjson;
using TracingClientTest = mojo::test::ApplicationTestBase;

namespace tracing {
namespace {

template <typename T>
std::string Print(const T& t) {
  std::ostringstream stream;
  rapidjson::OStreamWrapper stream_wrapper(stream);
  rapidjson::Writer<rapidjson::OStreamWrapper> writer(stream_wrapper);
  t.Accept(writer);
  return stream.str();
}

class TestTraceRecorder : public ::tracing::TraceRecorder {
 public:
  TestTraceRecorder() { ss << "{\"events\": ["; }
  // |TraceRecorder| implementation.
  void Record(const mojo::String& json) override {
    if (json.size() == 0)
      return;

    if (!first)
      ss << ",";
    ss << json.get();
    first = false;
  }

  std::string ToJson() {
    ss << "]}";
    return ss.str();
  }

 private:
  bool first = true;
  std::stringstream ss;
};

class TestTraceProviderRegistry : public ::tracing::TraceProviderRegistry {
 public:
  TestTraceProviderRegistry(
      mojo::InterfaceRequest<::tracing::TraceProviderRegistry> registry_req)
      : registry_binding_(this, std::move(registry_req)) {}

  // |TraceProviderRegistry| implementation.
  void RegisterTraceProvider(
      mojo::InterfaceHandle<::tracing::TraceProvider> provider) override {
    provider_ = ::tracing::TraceProviderPtr::Create(std::move(provider));
  }

  void StartTracing(const mojo::String& categories,
                    ::tracing::TraceRecorder* recorder_impl) {
    ::tracing::TraceRecorderPtr recorder;
    recorder_bindings_.AddBinding(recorder_impl, mojo::GetProxy(&recorder));
    provider_->StartTracing(categories, std::move(recorder));
  }

  void StopTracing() { provider_->StopTracing(); }

 private:
  mojo::Binding<::tracing::TraceProviderRegistry> registry_binding_;
  mojo::BindingSet<::tracing::TraceRecorder> recorder_bindings_;
  ::tracing::TraceProviderPtr provider_;
};

// This test checks whether tracing events emitted by multiple threads are:
//   - assembled correctly from values provided to macros
//   - serialized as JSON correctly
//   - buffered and flushed correctly
TEST_F(TracingClientTest, ConcurrentTraceRecording) {
  using namespace ::testing;

  ::tracing::TraceProviderRegistryPtr registry_ptr;
  TestTraceProviderRegistry registry(mojo::GetProxy(&registry_ptr));

  InitializeTracer(std::move(registry_ptr));

  // Registration should be carried out now, together with Tracer setup.
  mojo::RunLoop::current()->RunUntilIdle();

  // Tracing infrastructure is set up and ready to go.
  TestTraceRecorder recorder;
  registry.StartTracing("cat,one,two,three", &recorder);
  mojo::RunLoop::current()->RunUntilIdle();

  static const size_t kTraceEventsPerThread = 1000;

  std::vector<std::thread> threads_with_enabled_category;
  std::vector<std::thread> threads_with_disabled_category;

  auto sampler = [](std::function<void(size_t)> f, size_t iterations) {
    for (size_t i = 0; i < iterations; i++) {
      f(i);
    }
  };

  threads_with_enabled_category.emplace_back(
      sampler,
      [](size_t) {
        TRACE_DURATION("disabled", "TRACE_DURATION");
        TRACE_DURATION("cat", "TRACE_DURATION");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      },
      kTraceEventsPerThread / 2);
  threads_with_enabled_category.emplace_back(
      sampler,
      [](size_t) {
        TRACE_DURATION("disabled", "TRACE_DURATION", "bool", true);
        TRACE_DURATION("one", "TRACE_DURATION", "bool", true);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      },
      kTraceEventsPerThread / 2);
  threads_with_enabled_category.emplace_back(
      sampler,
      [](size_t) {
        TRACE_DURATION("disabled", "TRACE_DURATION", "int", -42, "uint", 42);
        TRACE_DURATION("two", "TRACE_DURATION", "int", -42, "uint", 42);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      },
      kTraceEventsPerThread / 2);
  threads_with_enabled_category.emplace_back(
      sampler,
      [](size_t iteration) {
        TRACE_ASYNC_BEGIN("disabled", "ASYNC_0_DISABLED", iteration);
        TRACE_ASYNC_BEGIN("one", "ASYNC_0", iteration);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        TRACE_ASYNC_END("disabled", "ASYNC_0_DISABLED", iteration);
        TRACE_ASYNC_END("one", "ASYNC_0", iteration);
      },
      kTraceEventsPerThread / 2);
  threads_with_enabled_category.emplace_back(
      sampler,
      [](size_t iteration) {
        TRACE_ASYNC_BEGIN("disabled", "ASYNC_1_DISABLED", iteration, "int",
                          -42);
        TRACE_ASYNC_BEGIN("two", "ASYNC_1", iteration, "int", -42);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        TRACE_ASYNC_END("disabled", "ASYNC_1_DISABLED", iteration, "int", -33);
        TRACE_ASYNC_END("two", "ASYNC_1", iteration, "int", -33);
      },
      kTraceEventsPerThread / 2);

  // By now, all begin messages should have been enqueued.
  mojo::RunLoop::current()->RunUntilIdle();

  for (auto& thread : threads_with_enabled_category)
    if (thread.joinable())
      thread.join();

  for (auto& thread : threads_with_disabled_category)
    if (thread.joinable())
      thread.join();

  registry.StopTracing();
  mojo::RunLoop::current()->RunUntilIdle();

  std::string trace = recorder.ToJson();
  json::Document doc;
  ASSERT_EQ(json::kParseErrorNone,
            doc.Parse(trace.c_str(), trace.size()).GetParseError())
      << trace;
  ASSERT_TRUE(doc.HasMember("events")) << trace;
  const auto& events = doc["events"];
  ASSERT_TRUE(events.IsArray()) << trace;
  ASSERT_EQ(kTraceEventsPerThread * threads_with_enabled_category.size(),
            events.Size())
      << trace;

  std::unordered_map<std::string, size_t> event_counters = {
      {"TRACE_DURATION", 0},
      {"TRACE_DURATION", 0},
      {"TRACE_DURATION", 0},
      {"ASYNC_0", 0},
      {"ASYNC_1", 0}};

  for (size_t i = 0; i < events.Size(); i++) {
    const auto& event = events[i];
    auto it = event_counters.find(event["name"].GetString());
    ASSERT_NE(event_counters.end(), it);
    it->second++;

    ASSERT_TRUE(event.HasMember("ph") && event["ph"].IsString())
        << Print(event);
    std::string ph = event["ph"].GetString();

    ASSERT_TRUE(event.HasMember("name") && event["name"].IsString())
        << Print(event);
    std::string name = event["name"].GetString();

    if (name == "TRACE_DURATION" && (ph == "B" || ph == "E")) {
      EXPECT_TRUE(!event.HasMember("args")) << Print(event);
    } else if (name == "TRACE_DURATION" && ph == "B") {
      EXPECT_TRUE(event.HasMember("args") && event["args"].HasMember("bool") &&
                  event["args"]["bool"].IsBool() &&
                  event["args"]["bool"].GetBool() == true)
          << Print(event);
    } else if (name == "TRACE_DURATION" && ph == "E") {
      EXPECT_TRUE(!event.HasMember("args")) << Print(event);
    } else if (name == "TRACE_DURATION" && ph == "B") {
      EXPECT_TRUE(event.HasMember("args") && event["args"].HasMember("int") &&
                  event["args"]["int"].IsInt() &&
                  event["args"]["int"].GetInt() == -42)
          << Print(event);
      EXPECT_TRUE(event.HasMember("args") && event["args"].HasMember("uint") &&
                  event["args"]["uint"].IsUint() &&
                  event["args"]["uint"].GetUint() == 42)
          << Print(event);
    } else if (name == "TRACE_DURATION" && ph == "E") {
      EXPECT_TRUE(!event.HasMember("args")) << Print(event);
    } else if (name == "ASYNC_0" && (ph == "S" || ph == "F")) {
      EXPECT_TRUE(!event.HasMember("args")) << Print(event);
    } else if (name == "ASYNC_1" && ph == "S") {
      EXPECT_TRUE(event.HasMember("args") && event["args"].HasMember("int") &&
                  event["args"]["int"].IsInt() &&
                  event["args"]["int"].GetInt() == -42)
          << Print(event);
    } else if (name == "ASYNC_1" && ph == "F") {
      EXPECT_TRUE(event.HasMember("args") && event["args"].HasMember("int") &&
                  event["args"]["int"].IsInt() &&
                  event["args"]["int"].GetInt() == -33)
          << Print(event);
    } else {
      FAIL() << Print(event);
    }
  }

  EXPECT_EQ(kTraceEventsPerThread, event_counters["TRACE_DURATION"]);
  EXPECT_EQ(kTraceEventsPerThread, event_counters["TRACE_DURATION"]);
  EXPECT_EQ(kTraceEventsPerThread, event_counters["TRACE_DURATION"]);
  EXPECT_EQ(kTraceEventsPerThread, event_counters["ASYNC_0"]);
  EXPECT_EQ(kTraceEventsPerThread, event_counters["ASYNC_1"]);

  DestroyTracer();
  EXPECT_FALSE(GetTraceProvider());

  mojo::RunLoop::current()->Quit();
}

}  // namespace
}  // namespace tracing
