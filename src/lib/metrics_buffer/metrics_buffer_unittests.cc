// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.cobalt/cpp/markers.h>
#include <fidl/fuchsia.cobalt/cpp/wire.h>
#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/sys/cpp/outgoing_directory.h>
#include <lib/zx/time.h>
#include <zircon/types.h>

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "lib/sys/cpp/service_directory.h"
#include "log.h"
#include "src/lib/metrics_buffer/metrics_buffer.h"

namespace {

class ServerAndClient {
 public:
  ServerAndClient() : loop_(&kAsyncLoopConfigNeverAttachToThread), dispatcher_(loop_.dispatcher()) {
    // Server end
    zx_status_t status = loop_.StartThread("MetricsBufferTest");
    ZX_ASSERT(status == ZX_OK);
    fidl::InterfaceHandle<fuchsia::io::Directory> aux_service_directory;
    zx::event server_create_done;
    status = zx::event::create(0, &server_create_done);
    ZX_ASSERT(status == ZX_OK);
    status = async::PostTask(dispatcher_, [this, &aux_service_directory, &server_create_done] {
      outgoing_aux_service_directory_parent_.emplace();
      zx_status_t status =
          outgoing_aux_service_directory_parent_->AddPublicService<fuchsia::cobalt::LoggerFactory>(
              [this](fidl::InterfaceRequest<fuchsia::cobalt::LoggerFactory> request) {
                fidl::ServerEnd<fuchsia_cobalt::LoggerFactory> llcpp_server_end(
                    request.TakeChannel());
                auto logger_factory = std::unique_ptr<LoggerFactory>(new LoggerFactory(this));
                // Ignore the result - logger_factory will be destroyed and the channel will be
                // closed on error.
                fidl::BindServer<LoggerFactory>(loop_.dispatcher(), std::move(llcpp_server_end),
                                                std::move(logger_factory));
              });
      outgoing_aux_service_directory_ =
          outgoing_aux_service_directory_parent_->GetOrCreateDirectory("svc");
      ZX_ASSERT(outgoing_aux_service_directory_);
      status = outgoing_aux_service_directory_->Serve(
          fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE |
              fuchsia::io::OPEN_FLAG_DIRECTORY,
          aux_service_directory.NewRequest().TakeChannel(), dispatcher_);
      ZX_ASSERT(status == ZX_OK);
      status = server_create_done.signal(0, ZX_EVENT_SIGNALED);
      ZX_ASSERT(status == ZX_OK);
    });
    ZX_ASSERT(status == ZX_OK);
    zx_signals_t pending;
    status = server_create_done.wait_one(ZX_EVENT_SIGNALED, zx::time::infinite(), &pending);
    ZX_ASSERT(status == ZX_OK);

    // Client end
    ZX_ASSERT(!aux_service_directory_);
    aux_service_directory_ =
        std::make_shared<sys::ServiceDirectory>(std::move(aux_service_directory));
    ZX_ASSERT(aux_service_directory_);
  }

  ~ServerAndClient() {
    aux_service_directory_ = nullptr;
    zx::event server_delete_done;
    zx_status_t status = zx::event::create(0, &server_delete_done);
    ZX_ASSERT(status == ZX_OK);
    status = async::PostTask(dispatcher_, [this, &server_delete_done] {
      outgoing_aux_service_directory_parent_.reset();
      zx_status_t status = server_delete_done.signal(0, ZX_EVENT_SIGNALED);
      ZX_ASSERT(status == ZX_OK);
    });
    ZX_ASSERT(status == ZX_OK);
    zx_signals_t pending;
    status = server_delete_done.wait_one(ZX_EVENT_SIGNALED, zx::time::infinite(), &pending);
    ZX_ASSERT(status == ZX_OK);
  }

  void WaitUntilEventCountAtLeast(uint32_t count) {
    while (true) {
      int64_t actual_count;
      {  // scope lock
        std::lock_guard<std::mutex> lock(lock_);
        actual_count = event_count_;
      }
      if (actual_count >= count) {
        return;
      }
      zx::nanosleep(zx::deadline_after(zx::msec(1)));
    }
  }

  uint32_t logger_factory_message_count() {
    std::lock_guard<std::mutex> lock(lock_);
    return logger_factory_message_count_;
  }

  uint32_t logger_message_count() {
    std::lock_guard<std::mutex> lock(lock_);
    return logger_message_count_;
  }

  uint32_t aggregated_events_count() {
    std::lock_guard<std::mutex> lock(lock_);
    return aggregated_events_count_;
  }

  int64_t max_count_per_aggregated_event() {
    std::lock_guard<std::mutex> lock(lock_);
    return max_count_per_aggregated_event_;
  }

  int64_t event_count() {
    std::lock_guard<std::mutex> lock(lock_);
    return event_count_;
  }

  struct LastEvent {
    LastEvent() = default;
    LastEvent(const LastEvent& to_copy) = default;
    LastEvent& operator=(const LastEvent& to_copy) = default;
    LastEvent(LastEvent&& to_move) = default;
    LastEvent& operator=(LastEvent&& to_move) = default;
    ~LastEvent() = default;

    uint32_t project_id = 0;
    uint32_t metric_id = 0;
    std::vector<uint32_t> event_codes;
    int64_t count = 0;
    int64_t period_duration_micros = 0;
  };
  const LastEvent GetLastEvent() {
    std::lock_guard<std::mutex> lock(lock_);
    return last_event_;
  }

  void IncLoggerFactoryMessageCount() {
    std::lock_guard<std::mutex> lock(lock_);
    ++logger_factory_message_count_;
  }

  void IncLoggerMessageCount() {
    std::lock_guard<std::mutex> lock(lock_);
    ++logger_message_count_;
  }

  void RecordAggregatedEvent(uint32_t project_id, uint32_t metric_id,
                             std::vector<uint32_t> event_codes, int64_t count,
                             int64_t period_duration_micros) {
    std::lock_guard<std::mutex> lock(lock_);
    last_event_.project_id = project_id;
    last_event_.metric_id = metric_id;
    last_event_.event_codes = event_codes;
    last_event_.count = count;
    last_event_.period_duration_micros = period_duration_micros;
    ++aggregated_events_count_;
    max_count_per_aggregated_event_ = std::max(max_count_per_aggregated_event_, count);
    // This must go last, as the test is relying on >= release semantics here.
    event_count_ += count;
  }

  std::shared_ptr<sys::ServiceDirectory> aux_service_directory() {
    std::lock_guard<std::mutex> lock(lock_);
    return aux_service_directory_;
  }

 protected:
  class LoggerFactory : public fidl::WireServer<fuchsia_cobalt::LoggerFactory> {
   public:
    LoggerFactory(ServerAndClient* parent) : parent_(parent) {}
    void CreateLoggerFromProjectId(CreateLoggerFromProjectIdRequestView request,
                                   CreateLoggerFromProjectIdCompleter::Sync& completer) override {
      parent_->IncLoggerFactoryMessageCount();
      auto logger = std::unique_ptr<Logger>(new Logger(parent_, request->project_id));
      // ~logger on channel close
      fidl::BindServer<Logger>(parent_->dispatcher_, std::move(request->logger), std::move(logger));
      completer.Reply(fuchsia_cobalt::wire::Status::kOk);
    }

    void CreateLoggerSimpleFromProjectId(
        CreateLoggerSimpleFromProjectIdRequestView request,
        CreateLoggerSimpleFromProjectIdCompleter::Sync& completer) override {
      ZX_ASSERT_MSG(false, "message not expected");
      completer.Reply(fuchsia_cobalt::wire::Status::kOk);
    }

    void CreateLoggerFromProjectSpec(
        CreateLoggerFromProjectSpecRequestView request,
        CreateLoggerFromProjectSpecCompleter::Sync& completer) override {
      ZX_ASSERT_MSG(false, "message not expected");
      completer.Reply(fuchsia_cobalt::wire::Status::kOk);
    }

   private:
    ServerAndClient* parent_ = nullptr;
  };
  class Logger : public fidl::WireServer<fuchsia_cobalt::Logger> {
   public:
    Logger(ServerAndClient* parent, uint32_t project_id)
        : parent_(parent), project_id_(project_id) {}
    void LogEvent(LogEventRequestView request, LogEventCompleter::Sync& completer) override {
      ZX_ASSERT_MSG(false, "message not expected");
      completer.Reply(fuchsia_cobalt::wire::Status::kOk);
    }
    void LogEventCount(LogEventCountRequestView request,
                       LogEventCountCompleter::Sync& completer) override {
      ZX_ASSERT_MSG(false, "message not expected");
      completer.Reply(fuchsia_cobalt::wire::Status::kOk);
    }
    void LogElapsedTime(LogElapsedTimeRequestView request,
                        LogElapsedTimeCompleter::Sync& completer) override {
      ZX_ASSERT_MSG(false, "message not expected");
      completer.Reply(fuchsia_cobalt::wire::Status::kOk);
    }
    void LogFrameRate(LogFrameRateRequestView request,
                      LogFrameRateCompleter::Sync& completer) override {
      ZX_ASSERT_MSG(false, "message not expected");
      completer.Reply(fuchsia_cobalt::wire::Status::kOk);
    }
    void LogMemoryUsage(LogMemoryUsageRequestView request,
                        LogMemoryUsageCompleter::Sync& completer) override {
      ZX_ASSERT_MSG(false, "message not expected");
      completer.Reply(fuchsia_cobalt::wire::Status::kOk);
    }
    void StartTimer(StartTimerRequestView request, StartTimerCompleter::Sync& completer) override {
      ZX_ASSERT_MSG(false, "message not expected");
      completer.Reply(fuchsia_cobalt::wire::Status::kOk);
    }
    void EndTimer(EndTimerRequestView request, EndTimerCompleter::Sync& completer) override {
      ZX_ASSERT_MSG(false, "message not expected");
      completer.Reply(fuchsia_cobalt::wire::Status::kOk);
    }
    void LogIntHistogram(LogIntHistogramRequestView request,
                         LogIntHistogramCompleter::Sync& completer) override {
      ZX_ASSERT_MSG(false, "message not expected");
      completer.Reply(fuchsia_cobalt::wire::Status::kOk);
    }
    void LogCustomEvent(LogCustomEventRequestView request,
                        LogCustomEventCompleter::Sync& completer) override {
      ZX_ASSERT_MSG(false, "message not expected");
      completer.Reply(fuchsia_cobalt::wire::Status::kOk);
    }
    void LogCobaltEvent(LogCobaltEventRequestView request,
                        LogCobaltEventCompleter::Sync& completer) override {
      ZX_ASSERT_MSG(false, "message not expected");
      completer.Reply(fuchsia_cobalt::wire::Status::kOk);
    }
    void LogCobaltEvents(LogCobaltEventsRequestView request,
                         LogCobaltEventsCompleter::Sync& completer) override {
      parent_->IncLoggerMessageCount();
      for (auto& event : request->events) {
        parent_->RecordAggregatedEvent(
            project_id_, event.metric_id,
            std::vector<uint32_t>(event.event_codes.begin(), event.event_codes.end()),
            event.payload.event_count().count, event.payload.event_count().period_duration_micros);
      }
      completer.Reply(fuchsia_cobalt::wire::Status::kOk);
    }

   private:
    ServerAndClient* parent_ = nullptr;
    uint32_t project_id_ = 0;
  };

  std::mutex lock_;
  uint32_t logger_factory_message_count_ = 0;
  uint32_t logger_message_count_ = 0;
  // the "count" of delivered aggregated events doesn't matter for this counter
  uint32_t aggregated_events_count_ = 0;
  int64_t max_count_per_aggregated_event_ = 0;
  // the "count" of all delivered aggregated events is summed here
  int64_t event_count_ = 0;
  LastEvent last_event_;

  // server end
  async::Loop loop_;
  async_dispatcher_t* dispatcher_ = nullptr;
  std::optional<sys::OutgoingDirectory> outgoing_aux_service_directory_parent_;
  vfs::PseudoDir* outgoing_aux_service_directory_ = nullptr;

  // client end
  std::shared_ptr<sys::ServiceDirectory> aux_service_directory_;
};

class MetricsBufferTest : public ::testing::Test {
 protected:
  MetricsBufferTest() { ResetState(); }

  ServerAndClient& state() { return *state_; }

  std::shared_ptr<sys::ServiceDirectory> aux_service_directory() {
    return state_->aux_service_directory();
  }

  void ResetState() {
    state_.reset();
    state_.emplace();
  }

 private:
  std::optional<ServerAndClient> state_;
};

TEST_F(MetricsBufferTest, Direct) {
  auto& s = state();

  auto metrics_buffer = cobalt::MetricsBuffer::Create(42, s.aux_service_directory());
  metrics_buffer->SetMinLoggingPeriod(zx::msec(10));
  auto e = s.GetLastEvent();
  EXPECT_EQ(0u, e.project_id);

  metrics_buffer->LogEvent(12, {1, 2, 3});
  s.WaitUntilEventCountAtLeast(1);
  e = s.GetLastEvent();
  EXPECT_EQ(42u, e.project_id);
  EXPECT_EQ(12u, e.metric_id);
  EXPECT_EQ(std::vector<uint32_t>({1, 2, 3}), e.event_codes);
  EXPECT_EQ(1u, e.count);
  EXPECT_EQ(0u, e.period_duration_micros);

  metrics_buffer->LogEvent(13, {3, 2, 1});
  s.WaitUntilEventCountAtLeast(2);
  e = s.GetLastEvent();
  EXPECT_EQ(42u, e.project_id);
  EXPECT_EQ(13u, e.metric_id);
  EXPECT_EQ(std::vector<uint32_t>({3, 2, 1}), e.event_codes);
  EXPECT_EQ(1u, e.count);
  EXPECT_EQ(0u, e.period_duration_micros);

  zx::nanosleep(zx::deadline_after(zx::msec(10)));
  EXPECT_EQ(2LL, s.event_count());
  auto last_event = s.GetLastEvent();
  EXPECT_EQ(13u, last_event.metric_id);
  EXPECT_EQ(std::vector<uint32_t>({3, 2, 1}), last_event.event_codes);
}

TEST_F(MetricsBufferTest, ViaMetricBuffer) {
  auto& s = state();

  auto metrics_buffer = cobalt::MetricsBuffer::Create(42, s.aux_service_directory());
  metrics_buffer->SetMinLoggingPeriod(zx::msec(10));
  auto metric_buffer = metrics_buffer->CreateMetricBuffer(12);

  metric_buffer.LogEvent({1u});
  s.WaitUntilEventCountAtLeast(1);
  auto e = s.GetLastEvent();
  EXPECT_EQ(42u, e.project_id);
  EXPECT_EQ(12u, e.metric_id);
  EXPECT_EQ(std::vector<uint32_t>({1u}), e.event_codes);
  EXPECT_EQ(1u, e.count);
  EXPECT_EQ(0u, e.period_duration_micros);

  metric_buffer.LogEvent({2u, 1u});
  s.WaitUntilEventCountAtLeast(2);
  zx::nanosleep(zx::deadline_after(zx::msec(10)));
  EXPECT_EQ(2LL, s.event_count());
  e = s.GetLastEvent();
  EXPECT_EQ(42u, e.project_id);
  EXPECT_EQ(12u, e.metric_id);
  EXPECT_EQ(std::vector<uint32_t>({2u, 1u}), e.event_codes);
  EXPECT_EQ(1u, e.count);
  EXPECT_EQ(0u, e.period_duration_micros);
}

TEST_F(MetricsBufferTest, BatchingHappens) {
  uint32_t success_count = 0;
  uint32_t failure_count = 0;
  constexpr uint32_t kMaxTries = 50;
  for (uint32_t attempt = 0; attempt < kMaxTries; ++attempt) {
    // This is intentionally doing ResetState() for attempt 0 despite already having a fresh state,
    // to ensure we cover a ResetState() while we already have one.
    ResetState();
    auto& s = state();

    auto metrics_buffer = cobalt::MetricsBuffer::Create(42, s.aux_service_directory());
    metrics_buffer->SetMinLoggingPeriod(zx::msec(10));
    // The first might not batch because the first will be sent asap.
    metrics_buffer->LogEvent(12u, {1u});
    // These two may or may not batch with the first, but typically we'll see at least some batching
    // given three events.
    metrics_buffer->LogEvent(12u, {1u});
    metrics_buffer->LogEvent(12u, {1u});
    s.WaitUntilEventCountAtLeast(2);
    zx::nanosleep(zx::deadline_after(zx::msec(10)));
    if (s.logger_message_count() >= 3) {
      printf("logger_message_count >= 3\n");
      ++failure_count;
      continue;
    }
    if (s.aggregated_events_count() >= 3) {
      printf("aggregated_events_count >= 3\n");
      ++failure_count;
      continue;
    }
    int64_t max_count = s.max_count_per_aggregated_event();
    if (max_count < 2) {
      printf("max_count < 2\n");
      ++failure_count;
      continue;
    }

    ++success_count;
    // These are basically comments at this point in the code...
    EXPECT_LT(s.logger_message_count(), 3u);
    EXPECT_LT(s.aggregated_events_count(), 3u);
    auto e = s.GetLastEvent();
    EXPECT_EQ(42u, e.project_id);
    EXPECT_EQ(12u, e.metric_id);
    EXPECT_EQ(std::vector<uint32_t>({1u}), e.event_codes);
    EXPECT_GE(e.count, 1u);
    EXPECT_EQ(0u, e.period_duration_micros);
    break;
  }
  printf("success: %u went around again: %u\n", success_count, failure_count);
  EXPECT_EQ(1u, success_count);
  EXPECT_LT(failure_count, 50u);
}

}  // namespace
