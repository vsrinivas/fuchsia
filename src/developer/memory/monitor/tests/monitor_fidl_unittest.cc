// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/cobalt/cpp/fidl_test_base.h>
#include <fuchsia/hardware/ram/metrics/cpp/fidl_test_base.h>
#include <lib/async/cpp/executor.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/inspect/testing/cpp/inspect.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <future>

#include <gtest/gtest.h>
#include <src/cobalt/bin/testing/fake_logger.h>

#include "src/developer/memory/monitor/monitor.h"

namespace monitor {
namespace test {

using namespace fuchsia::memory;
using namespace memory;
using namespace monitor;

class MonitorFidlUnitTest : public gtest::TestLoopFixture {
 protected:
  MonitorFidlUnitTest()
      : monitor_(std::make_unique<Monitor>(context_provider_.TakeContext(), fxl::CommandLine{},
                                           dispatcher(), false, false, false)) {}

  void TearDown() override {
    monitor_.reset();
    TestLoopFixture::TearDown();
  }

  MonitorPtr monitor() {
    MonitorPtr monitor;
    context_provider_.ConnectToPublicService(monitor.NewRequest());
    return monitor;
  }

 private:
  sys::testing::ComponentContextProvider context_provider_;
  std::unique_ptr<Monitor> monitor_;
};

class WatcherForTest : public fuchsia::memory::Watcher {
 public:
  WatcherForTest(fit::function<void(uint64_t free_bytes)> on_change)
      : on_change_(std::move(on_change)) {}

  void OnChange(Stats stats) override { on_change_(stats.free_bytes); }

  void AddBinding(fidl::InterfaceRequest<Watcher> request) {
    bindings_.AddBinding(this, std::move(request));
  }

 private:
  fidl::BindingSet<Watcher> bindings_;
  fit::function<void(uint64_t free_bytes)> on_change_;
};

TEST_F(MonitorFidlUnitTest, FreeBytes) {
  bool got_free = false;
  WatcherForTest watcher([&got_free](uint64_t free_bytes) { got_free = true; });
  WatcherPtr watcher_ptr;
  watcher.AddBinding(watcher_ptr.NewRequest());

  monitor()->Watch(watcher_ptr.Unbind());
  RunLoopUntilIdle();
  EXPECT_TRUE(got_free);
}

class FakeRamDevice : public fuchsia::hardware::ram::metrics::testing::Device_TestBase {
 public:
  FakeRamDevice() = default;

  fidl::InterfaceRequestHandler<fuchsia::hardware::ram::metrics::Device> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    return [this,
            dispatcher](fidl::InterfaceRequest<fuchsia::hardware::ram::metrics::Device> request) {
      binding_.Bind(std::move(request), dispatcher);
    };
  }

  void MeasureBandwidth(fuchsia::hardware::ram::metrics::BandwidthMeasurementConfig config,
                        MeasureBandwidthCallback completer) override {
    auto mul = config.cycles_to_measure / 1024;

    fuchsia::hardware::ram::metrics::BandwidthInfo info = {};
    info.timestamp = zx::msec(1234).to_nsecs();
    info.frequency = 256 * 1024 * 1024;
    info.bytes_per_cycle = 1;
    info.channels[0].readwrite_cycles = 10 * mul;
    info.channels[1].readwrite_cycles = 20 * mul;
    info.channels[2].readwrite_cycles = 30 * mul;
    info.channels[3].readwrite_cycles = 40 * mul;
    info.channels[4].readwrite_cycles = 50 * mul;
    info.channels[5].readwrite_cycles = 60 * mul;
    info.channels[6].readwrite_cycles = 70 * mul;
    info.channels[7].readwrite_cycles = 80 * mul;

    fuchsia::hardware::ram::metrics::Device_MeasureBandwidth_Response response(info);
    auto result = fuchsia::hardware::ram::metrics::Device_MeasureBandwidth_Result::WithResponse(
        std::move(response));
    completer(std::move(result));
  }

  void NotImplemented_(const std::string& name) override { FAIL() << name; }

 private:
  fidl::Binding<fuchsia::hardware::ram::metrics::Device> binding_{this};
};

class MockLogger : public ::fuchsia::cobalt::testing::Logger_TestBase {
 public:
  void LogCobaltEvents(std::vector<fuchsia::cobalt::CobaltEvent> events,
                       LogCobaltEventsCallback callback) override {
    num_calls_++;
    num_events_ += events.size();
    callback(fuchsia::cobalt::Status::OK);
  }
  void LogEvent(uint32_t metric_id, uint32_t event_code,
                LogCobaltEventsCallback callback) override {
    num_calls_++;
    num_events_ += 1;
    callback(fuchsia::cobalt::Status::OK);
  }
  void NotImplemented_(const std::string& name) override {
    ASSERT_TRUE(false) << name << " is not implemented";
  }
  int num_calls() { return num_calls_; }
  int num_events() { return num_events_; }

 private:
  int num_calls_ = 0;
  int num_events_ = 0;
};

class MockLoggerFactory : public ::fuchsia::cobalt::testing::LoggerFactory_TestBase {
 public:
  MockLogger* logger() { return logger_.get(); }
  uint32_t received_project_id() { return received_project_id_; }

  void CreateLoggerFromProjectId(uint32_t project_id,
                                 ::fidl::InterfaceRequest<fuchsia::cobalt::Logger> logger,
                                 CreateLoggerFromProjectIdCallback callback) override {
    received_project_id_ = project_id;
    logger_.reset(new MockLogger());
    logger_bindings_.AddBinding(logger_.get(), std::move(logger));
    callback(fuchsia::cobalt::Status::OK);
  }

  void NotImplemented_(const std::string& name) override {
    ASSERT_TRUE(false) << name << " is not implemented";
  }

 private:
  uint32_t received_project_id_;
  std::unique_ptr<MockLogger> logger_;
  fidl::BindingSet<fuchsia::cobalt::Logger> logger_bindings_;
};

class MemoryBandwidthInspectTest : public gtest::TestLoopFixture {
 public:
  MemoryBandwidthInspectTest()
      : monitor_(std::make_unique<Monitor>(context_provider_.TakeContext(), fxl::CommandLine{},
                                           dispatcher(), false, false, false)),
        executor_(dispatcher()),
        ram_binding_(&fake_device_),
        logger_factory_(new MockLoggerFactory()) {
    // Create metrics
    auto service_provider = context_provider_.service_directory_provider();
    service_provider->AddService(factory_bindings_.GetHandler(logger_factory_.get(), dispatcher()));
    CreateMetrics();

    // Set RamDevice
    fuchsia::hardware::ram::metrics::DevicePtr ram_device;
    ram_binding_.Bind(ram_device.NewRequest());
    monitor_->SetRamDevice(std::move(ram_device));
  }

  void RunPromiseToCompletion(fit::promise<> promise) {
    bool done = false;
    executor_.schedule_task(std::move(promise).and_then([&]() { done = true; }));
    RunLoopUntilIdle();
    ASSERT_TRUE(done);
  }

  fit::result<inspect::Hierarchy> GetHierachyFromInspect() {
    fit::result<inspect::Hierarchy> hierarchy;
    RunPromiseToCompletion(
        inspect::ReadFromInspector(Inspector()).then([&](fit::result<inspect::Hierarchy>& result) {
          hierarchy = std::move(result);
        }));
    return hierarchy;
  }

  inspect::Inspector Inspector() { return *(monitor_->inspector_.inspector()); }

 private:
  void CreateMetrics() {
    // The Monitor will make asynchronous calls to the MockLogger*s that are also running in this
    // class/tests thread. So the call to the Monitor needs to be made on a different thread, such
    // that the MockLogger*s running on the main thread can respond to those calls.
    std::future<void /*fuchsia::cobalt::Logger_Sync**/> result = std::async([this]() {
      monitor_->CreateMetrics();
    });
    while (result.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready) {
      // Run the main thread's loop, allowing the MockLogger* objects to respond to requests.
      RunLoopUntilIdle();
    }
    return result.get();
  }

  sys::testing::ComponentContextProvider context_provider_;
  std::unique_ptr<Monitor> monitor_;
  async::Executor executor_;
  FakeRamDevice fake_device_;
  fidl::Binding<fuchsia::hardware::ram::metrics::Device> ram_binding_;
  std::unique_ptr<MockLoggerFactory> logger_factory_;
  fidl::BindingSet<fuchsia::cobalt::LoggerFactory> factory_bindings_;
};

TEST_F(MemoryBandwidthInspectTest, MemoryBandwidth) {
  RunLoopUntilIdle();
  fit::result<inspect::Hierarchy> result = GetHierachyFromInspect();
  ASSERT_TRUE(result.is_ok());
  auto hierarchy = result.take_value();

  auto* metric_node = hierarchy.GetByPath({Metrics::kInspectPlatformNodeName});
  ASSERT_TRUE(metric_node);

  auto* metric_memory = metric_node->GetByPath({Metrics::kMemoryBandwidthNodeName});
  ASSERT_TRUE(metric_memory);

  auto* readings = metric_memory->node().get_property<inspect::UintArrayValue>(Metrics::kReadings);
  ASSERT_TRUE(readings);
  EXPECT_EQ(Metrics::kMemoryBandwidthArraySize, readings->value().size());
  EXPECT_EQ(94369704u, readings->value()[0]);

  auto* timestamp = metric_memory->node().get_property<inspect::IntPropertyValue>(
      Metrics::kReadingMemoryTimestamp);
  ASSERT_TRUE(timestamp);
}

}  // namespace test
}  // namespace monitor
