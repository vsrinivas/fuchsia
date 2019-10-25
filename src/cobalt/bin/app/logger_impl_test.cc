// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/logger_impl.h"

#include <lib/fidl/cpp/binding_set.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <utility>

#include "src/lib/cobalt/cpp/cobalt_event_builder.h"
#include "third_party/cobalt/src/lib/util/posix_file_system.h"
#include "third_party/cobalt/src/logger/event_aggregator.h"
#include "third_party/cobalt/src/logger/logger_test_utils.h"

namespace cobalt {

class LoggerImplTest : public ::testing::Test {
 public:
  LoggerImplTest()
      : encoder_(encoder::ClientSecret::GenerateNewSecret(), nullptr),
        local_aggregate_proto_store_("/tmp/a", std::make_unique<util::PosixFileSystem>()),
        obs_history_proto_store_("/tmp/b", std::make_unique<util::PosixFileSystem>()),
        observation_writer_(nullptr, nullptr, nullptr),
        event_aggregator_(&encoder_, &observation_writer_, &local_aggregate_proto_store_,
                          &obs_history_proto_store_),
        validated_clock_(&system_clock_),
        undated_event_manager_(std::make_shared<logger::UndatedEventManager>(
            &encoder_, &event_aggregator_, &observation_writer_, nullptr)),
        logger_impl_(std::make_unique<logger::Logger>(
                         std::make_unique<logger::ProjectContext>(
                             1, "test", std::make_unique<cobalt::ProjectConfig>()),
                         &encoder_, &event_aggregator_, &observation_writer_, nullptr,
                         &validated_clock_, undated_event_manager_, &fake_logger_),
                     nullptr),
        logger_(&logger_impl_) {}

 private:
  logger::Encoder encoder_;
  util::ConsistentProtoStore local_aggregate_proto_store_;
  util::ConsistentProtoStore obs_history_proto_store_;
  logger::ObservationWriter observation_writer_;
  logger::EventAggregator event_aggregator_;
  util::IncrementingSystemClock system_clock_;
  util::FakeValidatedClock validated_clock_;
  std::shared_ptr<logger::UndatedEventManager> undated_event_manager_;

 protected:
  logger::testing::FakeLogger fake_logger_;
  LoggerImpl logger_impl_;
  fuchsia::cobalt::Logger *logger_;
};

TEST_F(LoggerImplTest, PauseDuringBatch) {
  EXPECT_EQ(fake_logger_.call_count(), 0);
  logger_->LogCobaltEvents({}, [](Status status) {});
  int one_event_call_count = fake_logger_.call_count();
  std::vector<fuchsia::cobalt::CobaltEvent> events;
  events.push_back(CobaltEventBuilder(1).as_event());
  events.push_back(CobaltEventBuilder(1).as_event());
  events.push_back(CobaltEventBuilder(1).as_event());
  events.push_back(CobaltEventBuilder(1).as_event());
  events.push_back(CobaltEventBuilder(1).as_event());
  logger_->LogCobaltEvents(std::move(events), [](Status status) {});
  EXPECT_EQ(fake_logger_.call_count(), 2 * one_event_call_count);
}

// Tests that if StartTimer() and EndTimer() are invoked when the LoggerImpl
// was constructed without a TimerManager, then instead of crashing we
// return an error.
TEST_F(LoggerImplTest, NoTimerPresent) {
  Status status = Status::OK;
  logger_->StartTimer(0u, 0u, "", "", 0u, 0u, [&status](Status s) { status = s; });
  EXPECT_EQ(status, Status::INTERNAL_ERROR);
  status = Status::OK;
  logger_->EndTimer("", 0u, 0u, [&status](Status s) { status = s; });
  EXPECT_EQ(status, Status::INTERNAL_ERROR);
}

}  // namespace cobalt
