// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/aggregate_and_upload_impl.h"

#include <vector>

#include "src/public/lib/status.h"
#include "src/public/lib/status_codes.h"
#include "third_party/cobalt/src/public/testing/fake_cobalt_service.h"

namespace cobalt {

class FakeService : public testing::FakeCobaltService {
 public:
  Status GenerateAggregatedObservations(uint32_t final_day_index_utc) override {
    if (status_codes_.empty()) {
      set_observation_error_code(StatusCode::OK);
    } else {
      set_observation_error_code(status_codes_.back());
      status_codes_.pop_back();
    }
    return testing::FakeCobaltService::GenerateAggregatedObservations(0);
  }

  void set_status_code_sequence(std::vector<cobalt::StatusCode> status_codes) {
    while (!status_codes.empty()) {
      status_codes_.push_back(status_codes.back());
      status_codes.pop_back();
    }
  }

  bool status_codes_cleared() { return status_codes_.empty(); }

 private:
  std::vector<cobalt::StatusCode> status_codes_ = {};
};

TEST(AggregateAndUploadImplTest, Succeeds) {
  cobalt::FakeService fake_service;
  fake_service.set_send_soon_succeeds(true);
  AggregateAndUploadImpl aggregate_and_upload_impl(&fake_service);
  bool callback_invoked = false;
  aggregate_and_upload_impl.AggregateAndUploadMetricEvents(
      [&callback_invoked]() { callback_invoked = true; });
  EXPECT_TRUE(callback_invoked);
}

TEST(AggregateAndUploadImplTest, RetryImmediately) {
  cobalt::FakeService fake_service;
  fake_service.set_send_soon_succeeds(true);
  fake_service.set_status_code_sequence({StatusCode::RESOURCE_EXHAUSTED});
  AggregateAndUploadImpl aggregate_and_upload_impl(&fake_service);
  bool callback_invoked = false;
  aggregate_and_upload_impl.AggregateAndUploadMetricEvents(
      [&callback_invoked]() { callback_invoked = true; });
  EXPECT_TRUE(callback_invoked);
  EXPECT_TRUE(fake_service.status_codes_cleared());
}

TEST(AggregateAndUploadImplTest, RetryWithExponentialBackoff) {
  cobalt::FakeService fake_service;
  fake_service.set_send_soon_succeeds(true);
  fake_service.set_status_code_sequence(
      {StatusCode::DATA_LOSS, StatusCode::ABORTED, StatusCode::INTERNAL, StatusCode::UNAVAILABLE});
  AggregateAndUploadImpl aggregate_and_upload_impl(&fake_service);
  bool callback_invoked = false;
  aggregate_and_upload_impl.AggregateAndUploadMetricEvents(
      [&callback_invoked]() { callback_invoked = true; });
  EXPECT_TRUE(callback_invoked);
  EXPECT_TRUE(fake_service.status_codes_cleared());
}

TEST(AggregateAndUploadImplTest, NoRetry) {
  cobalt::FakeService fake_service;
  fake_service.set_send_soon_succeeds(true);
  // A status code of FAILED_PRECONDITION should stop any further retries.
  fake_service.set_status_code_sequence({StatusCode::FAILED_PRECONDITION, StatusCode::ABORTED});
  AggregateAndUploadImpl aggregate_and_upload_impl(&fake_service);
  bool callback_invoked = false;
  aggregate_and_upload_impl.AggregateAndUploadMetricEvents(
      [&callback_invoked]() { callback_invoked = true; });
  EXPECT_TRUE(callback_invoked);
  EXPECT_FALSE(fake_service.status_codes_cleared());
}

}  // namespace cobalt
