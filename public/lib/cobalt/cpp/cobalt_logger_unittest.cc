// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/cobalt/cpp/cobalt_logger.h"
#include "garnet/public/lib/cobalt/cpp/cobalt_logger_impl.h"

#include <lib/async/default.h>
#include <lib/component/cpp/service_provider_impl.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fxl/macros.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/svc/cpp/service_provider_bridge.h>

namespace cobalt {
namespace {

constexpr char kFakeCobaltConfig[] = "FakeConfig";
constexpr int32_t kFakeCobaltMetricId = 2;

bool Equals(const OccurrenceEvent* e1, const OccurrenceEvent* e2) {
  return e1->metric_id() == e2->metric_id() &&
         e1->event_type_index() == e2->event_type_index();
}

bool Equals(const CountEvent* e1, const CountEvent* e2) {
  return e1->metric_id() == e2->metric_id() &&
         e1->event_type_index() == e2->event_type_index() &&
         e1->component() == e2->component() &&
         e1->period_duration_micros() == e2->period_duration_micros() &&
         e1->count() == e2->count();
}

bool Equals(const ElapsedTimeEvent* e1, const ElapsedTimeEvent* e2) {
  return e1->metric_id() == e2->metric_id() &&
         e1->event_type_index() == e2->event_type_index() &&
         e1->component() == e2->component() &&
         e1->elapsed_micros() == e2->elapsed_micros();
}

enum EventType {
  EVENT_OCCURRED,
  EVENT_COUNT,
  ELAPSED_TIME,
};

class FakeLoggerImpl : public fuchsia::cobalt::Logger {
 public:
  FakeLoggerImpl() {}

  void LogEvent(uint32_t metric_id, uint32_t event_type_index,
                LogEventCallback callback) override {
    RecordCall(EventType::EVENT_OCCURRED,
               std::make_unique<OccurrenceEvent>(metric_id, event_type_index));
    callback(fuchsia::cobalt::Status2::OK);
  }

  void LogEventCount(uint32_t metric_id, uint32_t event_type_index,
                     fidl::StringPtr component, int64_t period_duration_micros,
                     int64_t count, LogEventCountCallback callback) override {
    RecordCall(EventType::EVENT_COUNT, std::make_unique<CountEvent>(
                                    metric_id, event_type_index, component,
                                    period_duration_micros, count));
    callback(fuchsia::cobalt::Status2::OK);
  }

  void LogElapsedTime(uint32_t metric_id, uint32_t event_type_index,
                      fidl::StringPtr component, int64_t elapsed_micros,
                      LogElapsedTimeCallback callback) override {
    RecordCall(EventType::ELAPSED_TIME,
               std::make_unique<ElapsedTimeEvent>(metric_id, event_type_index,
                                                  component, elapsed_micros));
    callback(fuchsia::cobalt::Status2::OK);
  }

  void LogFrameRate(uint32_t metric_id, uint32_t event_type_index,
                    fidl::StringPtr component, float fps,
                    LogFrameRateCallback callback) override {}

  void LogMemoryUsage(uint32_t metric_id, uint32_t event_type_index,
                      fidl::StringPtr component, int64_t bytes,
                      LogMemoryUsageCallback callback) override {}

  void LogString(uint32_t metric_id, fidl::StringPtr s,
                 LogStringCallback callback) override {}

  void StartTimer(uint32_t metric_id, uint32_t event_type_index,
                  fidl::StringPtr component, fidl::StringPtr timer_id,
                  uint64_t timestamp, uint32_t timeout_s,
                  StartTimerCallback callback) override {}

  void EndTimer(fidl::StringPtr timer_id, uint64_t timestamp,
                uint32_t timeout_s, EndTimerCallback callback) override {}

  void ExpectCalledOnceWith(EventType type, const Event* expected) {
    ASSERT_EQ(1U, calls_[type].size());
    switch (type) {
      case EventType::EVENT_OCCURRED:
        EXPECT_TRUE(Equals(static_cast<const OccurrenceEvent*>(expected),
                           static_cast<OccurrenceEvent*>(calls_[type][0].get())));
        break;
      case EventType::EVENT_COUNT:
        EXPECT_TRUE(Equals(static_cast<const CountEvent*>(expected),
                           static_cast<CountEvent*>(calls_[type][0].get())));
        break;
      case EventType::ELAPSED_TIME:
        EXPECT_TRUE(
            Equals(static_cast<const ElapsedTimeEvent*>(expected),
                   static_cast<ElapsedTimeEvent*>(calls_[type][0].get())));
        break;
      default:
        ASSERT_TRUE(false);
    }
  }

 private:
  void RecordCall(EventType type, std::unique_ptr<Event> event) {
    calls_[type].push_back(std::move(event));
  }

  std::map<EventType, std::vector<std::unique_ptr<Event>>> calls_;
};

class FakeLoggerFactoryImpl : public fuchsia::cobalt::LoggerFactory {
 public:
  FakeLoggerFactoryImpl() {}

  void CreateLogger(fuchsia::cobalt::ProjectProfile2 profile,
                    fidl::InterfaceRequest<fuchsia::cobalt::Logger> request,
                    CreateLoggerCallback callback) override {
    logger_.reset(new FakeLoggerImpl());
    logger_bindings_.AddBinding(logger_.get(), std::move(request));
    callback(fuchsia::cobalt::Status2::OK);
  }

  void CreateLoggerExt(
      fuchsia::cobalt::ProjectProfile2 profile,
      fidl::InterfaceRequest<fuchsia::cobalt::LoggerExt> request,
      CreateLoggerExtCallback callback) override {
    callback(fuchsia::cobalt::Status2::OK);
  }

  void CreateLoggerSimple(
      fuchsia::cobalt::ProjectProfile2 profile,
      fidl::InterfaceRequest<fuchsia::cobalt::LoggerSimple> request,
      CreateLoggerSimpleCallback callback) override {
    callback(fuchsia::cobalt::Status2::OK);
  }

  FakeLoggerImpl* logger() { return logger_.get(); }

 private:
  std::unique_ptr<FakeLoggerImpl> logger_;
  fidl::BindingSet<fuchsia::cobalt::Logger> logger_bindings_;
};

class CobaltLoggerTest : public gtest::TestLoopFixture {
 public:
  CobaltLoggerTest() : context_(InitStartupContext()) {}
  ~CobaltLoggerTest() override {}

  component::StartupContext* context() { return context_.get(); }

  FakeLoggerImpl* logger() { return factory_impl_->logger(); }

 private:
  std::unique_ptr<component::StartupContext> InitStartupContext() {
    factory_impl_.reset(new FakeLoggerFactoryImpl());
    service_provider.AddService<fuchsia::cobalt::LoggerFactory>(
        [this](fidl::InterfaceRequest<fuchsia::cobalt::LoggerFactory> request) {
          factory_bindings_.AddBinding(factory_impl_.get(), std::move(request));
        });
    service_provider.AddService<fuchsia::sys::Environment>(
        [this](fidl::InterfaceRequest<fuchsia::sys::Environment> request) {
          app_environment_request_ = std::move(request);
        });
    service_provider.AddService<fuchsia::sys::Launcher>(
        [this](fidl::InterfaceRequest<fuchsia::sys::Launcher> request) {
          launcher_request_ = std::move(request);
        });
    return std::make_unique<component::StartupContext>(
        service_provider.OpenAsDirectory(), zx::channel());
  }

  component::ServiceProviderBridge service_provider;
  std::unique_ptr<FakeLoggerFactoryImpl> factory_impl_;
  std::unique_ptr<FakeLoggerImpl> logger_;
  std::unique_ptr<component::StartupContext> context_;
  fidl::BindingSet<fuchsia::cobalt::LoggerFactory> factory_bindings_;
  fidl::InterfaceRequest<fuchsia::sys::Launcher> launcher_request_;
  fidl::InterfaceRequest<fuchsia::sys::Environment> app_environment_request_;
  FXL_DISALLOW_COPY_AND_ASSIGN(CobaltLoggerTest);
};

TEST_F(CobaltLoggerTest, InitializeCobalt) {
  fsl::SizedVmo fake_cobalt_config;
  ASSERT_TRUE(fsl::VmoFromString(kFakeCobaltConfig, &fake_cobalt_config));
  fuchsia::cobalt::ProjectProfile2 profile{
      .config = std::move(fake_cobalt_config).ToTransport()};
  auto cobalt_logger = NewCobaltLogger(async_get_default_dispatcher(),
                                       context(), std::move(profile));
  RunLoopUntilIdle();
  EXPECT_NE(cobalt_logger, nullptr);
}

TEST_F(CobaltLoggerTest, LogEvent) {
  OccurrenceEvent event(kFakeCobaltMetricId, 123);
  fsl::SizedVmo fake_cobalt_config;
  ASSERT_TRUE(fsl::VmoFromString(kFakeCobaltConfig, &fake_cobalt_config));
  fuchsia::cobalt::ProjectProfile2 profile{
      .config = std::move(fake_cobalt_config).ToTransport()};
  auto cobalt_logger = NewCobaltLogger(async_get_default_dispatcher(),
                                       context(), std::move(profile));
  RunLoopUntilIdle();
  cobalt_logger->LogEvent(event.metric_id(), event.event_type_index());
  RunLoopUntilIdle();
  logger()->ExpectCalledOnceWith(EventType::EVENT_OCCURRED, &event);
}

TEST_F(CobaltLoggerTest, LogEventCount) {
  CountEvent event(kFakeCobaltMetricId, 123, "some_component", 2, 321);
  fsl::SizedVmo fake_cobalt_config;
  ASSERT_TRUE(fsl::VmoFromString(kFakeCobaltConfig, &fake_cobalt_config));
  fuchsia::cobalt::ProjectProfile2 profile{
      .config = std::move(fake_cobalt_config).ToTransport()};
  auto cobalt_logger = NewCobaltLogger(async_get_default_dispatcher(),
                                       context(), std::move(profile));
  RunLoopUntilIdle();
  cobalt_logger->LogEventCount(
      event.metric_id(), event.event_type_index(), event.component(),
      zx::duration(event.period_duration_micros() * ZX_USEC(1)), event.count());
  RunLoopUntilIdle();
  logger()->ExpectCalledOnceWith(EventType::EVENT_COUNT, &event);
}

TEST_F(CobaltLoggerTest, LogElapsedTime) {
  ElapsedTimeEvent event(kFakeCobaltMetricId, 123, "some_component", 321);
  fsl::SizedVmo fake_cobalt_config;
  ASSERT_TRUE(fsl::VmoFromString(kFakeCobaltConfig, &fake_cobalt_config));
  fuchsia::cobalt::ProjectProfile2 profile{
      .config = std::move(fake_cobalt_config).ToTransport()};
  auto cobalt_logger = NewCobaltLogger(async_get_default_dispatcher(),
                                       context(), std::move(profile));
  RunLoopUntilIdle();
  cobalt_logger->LogElapsedTime(
      event.metric_id(), event.event_type_index(), event.component(),
      zx::duration(event.elapsed_micros() * ZX_USEC(1)));
  RunLoopUntilIdle();
  logger()->ExpectCalledOnceWith(EventType::ELAPSED_TIME, &event);
}

}  // namespace
}  // namespace cobalt
