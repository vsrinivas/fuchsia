// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/cobalt/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fit/function.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>
#include <limits.h>
#include <zircon/assert.h>

#include <cstdint>
#include <memory>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <cobalt-client/cpp/collector_internal.h>
#include <cobalt-client/cpp/in_memory_logger.h>
#include <cobalt-client/cpp/metric_options.h>
#include <cobalt-client/cpp/types_internal.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <zxtest/zxtest.h>

namespace cobalt_client {
namespace internal {
namespace {

using Status = ::llcpp::fuchsia::cobalt::Status;
using EventData = ::llcpp::fuchsia::cobalt::EventPayload;

// Fake Implementation for fuchsia::cobalt::LoggerFactory.
class FakeLoggerFactoryService : public ::llcpp::fuchsia::cobalt::LoggerFactory::Interface {
 public:
  void CreateLoggerFromProjectId(uint32_t project_id, ::zx::channel logger,
                                 CreateLoggerFromProjectIdCompleter::Sync& completer) final {
    completer.Reply(create_logger_handler_(project_id, std::move(logger)));
  }

  void CreateLoggerSimpleFromProjectId(
      uint32_t project_id, ::zx::channel logger,
      CreateLoggerSimpleFromProjectIdCompleter::Sync& completer) final {
    ZX_PANIC("Not Implemented.");
  }

  void CreateLoggerFromProjectSpec(uint32_t customer_id, uint32_t project_id, ::zx::channel logger,
                                   CreateLoggerFromProjectSpecCompleter::Sync& completer) final {
    ZX_PANIC("Not Implemented.");
  }

  void set_create_logger_handler(fit::function<Status(uint32_t, zx::channel)> handler) {
    create_logger_handler_ = std::move(handler);
  }

 private:
  fit::function<Status(uint32_t, zx::channel)> create_logger_handler_;
};

// Fake Implementation for fuchsia::cobalt::Logger.
class FakeLoggerService : public ::llcpp::fuchsia::cobalt::Logger::Interface {
 public:
  void LogEvent(uint32_t metric_id, uint32_t event_code, LogEventCompleter::Sync& completer) final {
    ZX_PANIC("Not Implemented.");
  }

  void LogEventCount(uint32_t metric_id, uint32_t event_code, ::fidl::StringView component,
                     int64_t period_duration_micros, int64_t count,
                     LogEventCountCompleter::Sync& completer) {
    ZX_PANIC("Not Implemented.");
  }

  void LogElapsedTime(uint32_t metric_id, uint32_t event_code, ::fidl::StringView component,
                      int64_t elapsed_micros, LogElapsedTimeCompleter::Sync& completer) final {
    ZX_PANIC("Not Implemented.");
  }

  void LogFrameRate(uint32_t metric_id, uint32_t event_code, ::fidl::StringView component,
                    float fps, LogFrameRateCompleter::Sync& completer) final {
    ZX_PANIC("Not Implemented.");
  }

  void LogMemoryUsage(uint32_t metric_id, uint32_t event_code, ::fidl::StringView component,
                      int64_t bytes, LogMemoryUsageCompleter::Sync& completer) final {
    ZX_PANIC("Not Implemented.");
  }

  void StartTimer(uint32_t metric_id, uint32_t event_code, ::fidl::StringView component,
                  ::fidl::StringView timer_id, uint64_t timestamp, uint32_t timeout_s,
                  StartTimerCompleter::Sync& completer) final {
    ZX_PANIC("Not Implemented.");
  }

  void EndTimer(::fidl::StringView timer_id, uint64_t timestamp, uint32_t timeout_s,
                EndTimerCompleter::Sync& completer) {
    ZX_PANIC("Not Implemented.");
  }

  void LogIntHistogram(uint32_t metric_id, uint32_t event_code, ::fidl::StringView component,
                       ::fidl::VectorView<::llcpp::fuchsia::cobalt::HistogramBucket> histogram,
                       LogIntHistogramCompleter::Sync& completer) final {
    ZX_PANIC("Not Implemented.");
  }

  void LogCustomEvent(uint32_t metric_id,
                      ::fidl::VectorView<::llcpp::fuchsia::cobalt::CustomEventValue> event_values,
                      LogCustomEventCompleter::Sync& completer) final {
    ZX_PANIC("Not Implemented.");
  }

  void LogCobaltEvent(::llcpp::fuchsia::cobalt::CobaltEvent event,
                      LogCobaltEventCompleter::Sync& completer) final {
    // Use MetricOptions as a key.
    MetricOptions info;
    info.metric_dimensions = static_cast<uint32_t>(event.event_codes.count());
    if (event_code_count_tracker_ != nullptr) {
      *event_code_count_tracker_ = info.metric_dimensions;
    }
    info.metric_id = event.metric_id;
    info.component = event.component.data();
    for (uint64_t i = 0; i < MetricOptions::kMaxEventCodes; ++i) {
      if (i < info.metric_dimensions) {
        info.event_codes[i] = event.event_codes[i];
      } else {
        info.event_codes[i] = 0;
      }
    }

    switch (event.payload.which()) {
      case EventData::Tag::kIntHistogram:
        storage_.Log(info, event.payload.int_histogram().data(),
                     event.payload.int_histogram().count());
        break;
      case EventData::Tag::kEventCount:
        storage_.Log(info, event.payload.event_count().count);
        break;
      default:
        ZX_ASSERT_MSG(false, "Not Supported.");
        break;
    }
    completer.Reply(log_return_status_);
  }

  void LogCobaltEvents(::fidl::VectorView<::llcpp::fuchsia::cobalt::CobaltEvent> events,
                       LogCobaltEventsCompleter::Sync& completer) final {
    ZX_PANIC("Not Implemented.");
  }

  void set_log_return_status(Status status) { log_return_status_ = status; }

  void set_log_event_code_count_tracker(uint32_t* event_count) {
    event_code_count_tracker_ = event_count;
  }

  // Returns the |InMemoryLogger| used for backing the storage of this |cobalt.Logger|.
  const InMemoryLogger& storage() const { return storage_; }

 private:
  Status log_return_status_ = Status::OK;
  uint32_t* event_code_count_tracker_ = nullptr;
  InMemoryLogger storage_;
};

// Struct for argument validation.
struct CreateLoggerValidationArgs {
  void Check() const {
    EXPECT_TRUE(is_id_ok);
    EXPECT_TRUE(is_channel_ok);
  }

  uint32_t project_id;

  // Return status for the fidl call.
  Status return_status = Status::OK;

  // Used for validating the args and validation on the main thread.
  fbl::Mutex result_lock_;
  bool is_id_ok = false;
  bool is_channel_ok = false;
};

void BindLoggerFactoryService(FakeLoggerFactoryService* bindee, zx::channel channel,
                              async_dispatcher_t* dispatcher) {
  fidl::BindSingleInFlightOnly(dispatcher, std::move(channel), bindee);
}

void BindLoggerToLoggerFactoryService(FakeLoggerFactoryService* binder, FakeLoggerService* bindee,
                                      CreateLoggerValidationArgs* checker,
                                      async_dispatcher_t* dispatcher) {
  binder->set_create_logger_handler(
      [bindee, checker, dispatcher](uint32_t project_id, zx::channel channel) {
        fbl::AutoLock lock(&checker->result_lock_);
        checker->is_id_ok = (checker->project_id == project_id);
        checker->is_channel_ok = channel.is_valid();
        fidl::BindSingleInFlightOnly(dispatcher, std::move(channel), bindee);

        return checker->return_status;
      });
}

constexpr uint32_t kProjectId = 1234;

class LoggerServiceFixture : public zxtest::Test {
 public:
  void SetUp() final {
    // Initialize the service loop.
    service_loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);

    checker_.project_id = kProjectId;
    checker_.return_status = Status::OK;

    BindLoggerToLoggerFactoryService(&logger_factory_impl_, &logger_impl_, &checker_,
                                     service_loop_->dispatcher());
  }

  std::unique_ptr<CobaltLogger> MakeLogger() { return std::make_unique<CobaltLogger>(Options()); }

  CobaltOptions Options() {
    CobaltOptions options;
    options.project_id = kProjectId;
    options.service_connect = [this](const char* path, zx::channel service_channel) {
      BindLoggerFactoryService(&logger_factory_impl_, std::move(service_channel),
                               service_loop_->dispatcher());
      return ZX_OK;
    };
    return options;
  }

  void StartServiceLoop() {
    ASSERT_NOT_NULL(service_loop_);
    ASSERT_TRUE(service_loop_->GetState() == ASYNC_LOOP_RUNNABLE);
    service_loop_->StartThread("LoggerServiceThread");
  }

  void StopServiceLoop() {
    service_loop_->Quit();
    service_loop_->JoinThreads();
    service_loop_->ResetQuit();
  }

  void TearDown() final { StopServiceLoop(); }

  const InMemoryLogger& GetStorage() const { return logger_impl_.storage(); }

  async::Loop* GetLoop() { return service_loop_.get(); }

  void SetLoggerLogReturnStatus(Status status) { logger_impl_.set_log_return_status(status); }
  void SetLoggerLogEventCountTracker(uint32_t* event_code_count_tracker) {
    logger_impl_.set_log_event_code_count_tracker(event_code_count_tracker);
  }

 protected:
  CreateLoggerValidationArgs checker_;

 private:
  std::unique_ptr<async::Loop> service_loop_ = nullptr;

  FakeLoggerFactoryService logger_factory_impl_;
  FakeLoggerService logger_impl_;
};

using CobaltLoggerTest = LoggerServiceFixture;

constexpr uint64_t kBucketCount = 10;

constexpr int64_t kCounter = 1;

TEST_F(CobaltLoggerTest, LogHistogramReturnsTrueWhenServiceReturnsOk) {
  auto logger = MakeLogger();
  std::vector<HistogramBucket> buckets;

  MetricOptions info;
  uint32_t event_code_count_tracker = 0;

  info.metric_id = 1;
  info.component = "SomeComponent";
  info.event_codes = {1, 2, 3, 0, 0};
  info.metric_dimensions = 3;
  SetLoggerLogEventCountTracker(&event_code_count_tracker);

  for (uint32_t i = 0; i < kBucketCount; ++i) {
    buckets.push_back({.index = i, .count = 2 * i});
  }

  ASSERT_NO_FAILURES(StartServiceLoop(), "Failed to initialize the service async dispatchers.");

  ASSERT_TRUE(logger->Log(info, buckets.data(), buckets.size()));
  ASSERT_NO_FATAL_FAILURES(checker_.Check());
  auto itr = GetStorage().histograms().find(info);
  ASSERT_NE(GetStorage().histograms().end(), itr);
  ASSERT_EQ(itr->second.size(), kBucketCount);

  EXPECT_EQ(info.metric_dimensions, event_code_count_tracker);

  for (uint32_t i = 0; i < itr->second.size(); ++i) {
    EXPECT_EQ(buckets[i].count, (itr->second).at(i));
  }
}

TEST_F(CobaltLoggerTest, LogHistogramReturnsFalseWhenFactoryServiceReturnsError) {
  auto logger = MakeLogger();
  std::vector<HistogramBucket> buckets;

  MetricOptions info;
  info.metric_id = 1;
  info.component = "SomeComponent";
  info.event_codes = {1, 2, 3, 4, 5};
  info.metric_dimensions = MetricOptions::kMaxEventCodes;

  checker_.return_status = Status::INTERNAL_ERROR;

  for (uint32_t i = 0; i < kBucketCount; ++i) {
    buckets.push_back({.index = i, .count = 2 * i});
  }

  ASSERT_NO_FAILURES(StartServiceLoop(), "Failed to initialize the service async dispatchers.");

  ASSERT_FALSE(logger->Log(info, buckets.data(), buckets.size()));
  ASSERT_NO_FATAL_FAILURES(checker_.Check());
  EXPECT_TRUE(GetStorage().histograms().empty());
  EXPECT_TRUE(GetStorage().counters().empty());
}

TEST_F(CobaltLoggerTest, LogHistogramReturnsFalseWhenLoggerServiceReturnsError) {
  auto logger = MakeLogger();
  std::vector<HistogramBucket> buckets;

  MetricOptions info;
  info.metric_id = 1;
  info.component = "SomeComponent";
  info.event_codes = {1, 2, 3, 4, 5};
  info.metric_dimensions = MetricOptions::kMaxEventCodes;

  SetLoggerLogReturnStatus(Status::INTERNAL_ERROR);

  for (uint32_t i = 0; i < kBucketCount; ++i) {
    buckets.push_back({.index = i, .count = 2 * i});
  }

  ASSERT_NO_FAILURES(StartServiceLoop(), "Failed to initialize the service async dispatchers.");

  ASSERT_FALSE(logger->Log(info, buckets.data(), buckets.size()));
  ASSERT_NO_FATAL_FAILURES(checker_.Check());
}

TEST_F(CobaltLoggerTest, LogHistogramWaitsUntilServiceBecomesAvailable) {
  auto logger = MakeLogger();
  std::vector<HistogramBucket> buckets;
  std::atomic<bool> log_result(false);

  MetricOptions info;
  info.metric_id = 1;
  info.component = "SomeComponent";
  info.event_codes = {1, 2, 3, 4, 5};
  info.metric_dimensions = MetricOptions::kMaxEventCodes;

  for (uint32_t i = 0; i < kBucketCount; ++i) {
    buckets.push_back({.index = i, .count = 2 * i});
  }

  std::thread blocks_until_starts(
      [info, &log_result, &buckets](internal::Logger* logger) {
        log_result.store(logger->Log(info, buckets.data(), buckets.size()));
      },
      logger.get());

  ASSERT_NO_FAILURES(StartServiceLoop(), "Failed to initialize the service async dispatchers.");

  // This should wait until Log finishes.
  blocks_until_starts.join();

  ASSERT_TRUE(log_result);
  ASSERT_NO_FATAL_FAILURES(checker_.Check());
  auto itr = GetStorage().histograms().find(info);
  ASSERT_NE(GetStorage().histograms().end(), itr);
  ASSERT_EQ(itr->second.size(), kBucketCount);

  for (uint32_t i = 0; i < itr->second.size(); ++i) {
    EXPECT_EQ(buckets[i].count, (itr->second).at(i));
  }
}

TEST_F(CobaltLoggerTest, LogCounterReturnsTrueWhenServiceReturnsOk) {
  auto logger = MakeLogger();
  MetricOptions info;
  info.metric_id = 1;
  info.component = "SomeComponent";
  info.event_codes = {1, 2, 3, 4, 5};
  info.metric_dimensions = MetricOptions::kMaxEventCodes;

  ASSERT_NO_FAILURES(StartServiceLoop(), "Failed to initialize the service async dispatchers.");

  ASSERT_TRUE(logger->Log(info, kCounter));
  ASSERT_NO_FATAL_FAILURES(checker_.Check());
  auto itr = GetStorage().counters().find(info);
  ASSERT_NE(GetStorage().counters().end(), itr);

  EXPECT_EQ(itr->second, kCounter);
}

TEST_F(CobaltLoggerTest, LogCounterReturnsFalseWhenFactoryServiceReturnsError) {
  auto logger = MakeLogger();
  MetricOptions info;
  info.metric_id = 1;
  info.component = "SomeComponent";
  info.event_codes = {1, 2, 3, 4, 5};
  info.metric_dimensions = MetricOptions::kMaxEventCodes;
  checker_.return_status = Status::INTERNAL_ERROR;

  ASSERT_NO_FAILURES(StartServiceLoop(), "Failed to initialize the service async dispatchers.");

  ASSERT_FALSE(logger->Log(info, kCounter));
  ASSERT_NO_FATAL_FAILURES(checker_.Check());
  EXPECT_TRUE(GetStorage().histograms().empty());
  EXPECT_TRUE(GetStorage().counters().empty());
}

TEST_F(CobaltLoggerTest, LogCounterReturnsFalseWhenLoggerServiceReturnsError) {
  auto logger = MakeLogger();
  MetricOptions info;
  info.metric_id = 1;
  info.component = "SomeComponent";
  info.event_codes = {1, 2, 3, 4, 5};
  info.metric_dimensions = MetricOptions::kMaxEventCodes;

  SetLoggerLogReturnStatus(Status::INTERNAL_ERROR);

  ASSERT_NO_FAILURES(StartServiceLoop(), "Failed to initialize the service async dispatchers.");

  ASSERT_FALSE(logger->Log(info, kCounter));
  ASSERT_NO_FATAL_FAILURES(checker_.Check());
}

TEST_F(CobaltLoggerTest, LogCounterWaitsUntilServiceBecomesAvailable) {
  auto logger = MakeLogger();
  std::atomic<bool> log_result(false);
  MetricOptions info;
  info.metric_id = 1;
  info.component = "SomeComponent";
  info.event_codes = {1, 2, 3, 4, 5};
  info.metric_dimensions = MetricOptions::kMaxEventCodes;

  std::thread blocks_until_starts(
      [info, &log_result](internal::Logger* logger) {
        log_result.store(logger->Log(info, kCounter));
      },
      logger.get());

  ASSERT_NO_FAILURES(StartServiceLoop(), "Failed to initialize the service async dispatchers.");

  // This should wait until Log finishes.
  blocks_until_starts.join();

  ASSERT_TRUE(log_result.load());
  ASSERT_NO_FATAL_FAILURES(checker_.Check());
  auto itr = GetStorage().counters().find(info);
  ASSERT_NE(GetStorage().counters().end(), itr);

  EXPECT_EQ(itr->second, kCounter);
}

}  // namespace
}  // namespace internal
}  // namespace cobalt_client
