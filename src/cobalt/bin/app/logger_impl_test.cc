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
#include "third_party/cobalt/config/client_config.h"
#include "third_party/cobalt/logger/logger_test_utils.h"
#include "third_party/cobalt/util/posix_file_system.h"

namespace cobalt {

class LoggerImplTest : public ::testing::Test {
 public:
  LoggerImplTest()
      : encoder_(encoder::ClientSecret::GenerateNewSecret(), nullptr),
        local_aggregate_proto_store_("/tmp/a",
                                     std::make_unique<util::PosixFileSystem>()),
        obs_history_proto_store_("/tmp/b",
                                 std::make_unique<util::PosixFileSystem>()),
        observation_writer_(nullptr, nullptr, nullptr),
        event_aggregator_(&encoder_, &observation_writer_,
                          &local_aggregate_proto_store_,
                          &obs_history_proto_store_),
        logger_impl_(std::make_unique<logger::ProjectContext>(
                         1, "test", std::make_unique<cobalt::ProjectConfig>()),
                     &encoder_, &event_aggregator_, &observation_writer_,
                     nullptr, &fake_logger_),
        logger_(&logger_impl_) {}

 private:
  logger::Encoder encoder_;
  util::ConsistentProtoStore local_aggregate_proto_store_;
  util::ConsistentProtoStore obs_history_proto_store_;
  logger::ObservationWriter observation_writer_;
  logger::EventAggregator event_aggregator_;

 protected:
  logger::testing::FakeLogger fake_logger_;
  LoggerImpl logger_impl_;
  fuchsia::cobalt::Logger *logger_;
};

TEST_F(LoggerImplTest, PauseDuringBatch) {
  EXPECT_EQ(fake_logger_.call_count(), 0);
  logger_->LogCobaltEvents({}, [](Status status) {});
  EXPECT_EQ(fake_logger_.call_count(), 1);
  std::vector<fuchsia::cobalt::CobaltEvent> events;
  events.push_back(CobaltEventBuilder(1).as_event());
  events.push_back(CobaltEventBuilder(1).as_event());
  events.push_back(CobaltEventBuilder(1).as_event());
  events.push_back(CobaltEventBuilder(1).as_event());
  events.push_back(CobaltEventBuilder(1).as_event());
  logger_->LogCobaltEvents(std::move(events), [](Status status) {});
  EXPECT_EQ(fake_logger_.call_count(), 2);
}

}  // namespace cobalt
