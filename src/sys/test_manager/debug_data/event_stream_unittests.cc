// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/debugdata/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_ptr.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/rights.h>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <src/lib/fxl/strings/string_printf.h>
#include <src/lib/testing/loop_fixture/real_loop_fixture.h>

#include "abstract_data_processor.h"
#include "common.h"
#include "event_stream.h"

using EventStreamTest = gtest::RealLoopFixture;

class TestDataProcessor : public AbstractDataProcessor {
 public:
  explicit TestDataProcessor(
      std::shared_ptr<std::vector<std::pair<std::string, std::vector<DataSinkDump>>>> vec)
      : vec_(std::move(vec)) {}

  void ProcessData(std::string test_url, std::vector<DataSinkDump> data_sink_vec) override {
    vec_->emplace_back(std::move(test_url), std::move(data_sink_vec));
  }
  std::shared_ptr<std::vector<std::pair<std::string, std::vector<DataSinkDump>>>> vec_;
};

class TestInfo : public fuchsia::test::internal::Info {
 public:
  TestInfo() = default;
  explicit TestInfo(std::map<std::string, std::string> map) : map_(std::move(map)) {}

  fidl::InterfacePtr<fuchsia::test::internal::Info> GetPtr() {
    fidl::InterfacePtr<fuchsia::test::internal::Info> info;
    bindings_.AddBinding(this, info.NewRequest());
    return info;
  }

  void AddMapping(std::string moniker, std::string test_url) {
    map_[std::move(moniker)] = std::move(test_url);
  }

  void GetTestUrl(std::string moniker, GetTestUrlCallback callback) override {
    fuchsia::test::internal::Info_GetTestUrl_Result result;
    auto it = map_.find(moniker);
    if (it != map_.end()) {
      result.set_response(fuchsia::test::internal::Info_GetTestUrl_Response(it->second));
    } else {
      result.set_err(ZX_ERR_NOT_FOUND);
    }
    callback(std::move(result));
  }

 private:
  std::map<std::string, std::string> map_;
  fidl::BindingSet<fuchsia::test::internal::Info> bindings_;
};

fuchsia::debugdata::DebugDataPtr SendCapabilityRequestedEvent(
    const fidl::InterfacePtr<fuchsia::sys2::EventStream>& event_stream_ptr, std::string moniker) {
  fuchsia::debugdata::DebugDataPtr debug_data_ptr;
  fuchsia::sys2::Event event;

  fuchsia::sys2::EventHeader header;
  header.set_moniker(std::move(moniker));
  header.set_event_type(fuchsia::sys2::EventType::CAPABILITY_REQUESTED);
  event.set_header(std::move(header));

  fuchsia::sys2::EventResult event_result;
  fuchsia::sys2::EventPayload payload;
  fuchsia::sys2::CapabilityRequestedPayload p;
  p.set_capability(debug_data_ptr.NewRequest().TakeChannel());
  payload.set_capability_requested(std::move(p));
  event_result.set_payload(std::move(payload));
  event.set_event_result(std::move(event_result));

  event_stream_ptr->OnEvent(std::move(event));

  return debug_data_ptr;
}

void SendComponentStopEvent(const fidl::InterfacePtr<fuchsia::sys2::EventStream>& event_stream_ptr,
                            std::string moniker) {
  fuchsia::sys2::Event event;

  fuchsia::sys2::EventHeader header;
  header.set_moniker(std::move(moniker));
  header.set_event_type(fuchsia::sys2::EventType::STOPPED);
  event.set_header(std::move(header));

  event_stream_ptr->OnEvent(std::move(event));
}

zx::vmo GetVmo() {
  zx::vmo vmo;
  zx::vmo::create(100, 0, &vmo);
  return vmo;
}

TEST_F(EventStreamTest, Test) {
  auto shared_vec =
      std::make_shared<std::vector<std::pair<std::string, std::vector<DataSinkDump>>>>();
  std::unique_ptr<AbstractDataProcessor> data_sink_processor =
      std::make_unique<TestDataProcessor>(shared_vec);
  std::map<std::string, std::string> moniker_url_map = {
      {"foo/bar", "foo_bar.cm"}, {"foo", "foo.cm"}, {"bar/foo", "bar_foo.cm"}};
  TestInfo info(moniker_url_map);

  fidl::InterfacePtr<fuchsia::sys2::EventStream> event_stream_ptr;
  EventStreamImpl event_stream(event_stream_ptr.NewRequest(), info.GetPtr(),
                               std::move(data_sink_processor), dispatcher());

  // Test simple case, where events are in order
  auto foo_bar_ptr = SendCapabilityRequestedEvent(event_stream_ptr, "foo/bar");
  fuchsia::debugdata::DebugDataVmoTokenPtr token_1, token_2, token_3;
  foo_bar_ptr->Publish("foo_bar_sink1", GetVmo(), token_1.NewRequest());
  foo_bar_ptr->Publish("foo_bar_sink2", GetVmo(), token_2.NewRequest());
  foo_bar_ptr->Publish("foo_bar_sink3", GetVmo(), token_3.NewRequest());

  RunLoopUntilIdle();
  // as we have not closed the connection, we should not get the VMOs
  ASSERT_EQ(shared_vec->size(), 0u);
  foo_bar_ptr.Unbind();
  SendComponentStopEvent(event_stream_ptr, "foo/bar");

  RunLoopUntilIdle();
  ASSERT_EQ(shared_vec->size(), 1u);
  ASSERT_EQ(shared_vec->at(0).second.size(), 3u);
  ASSERT_EQ(shared_vec->at(0).first, "foo_bar.cm");
  shared_vec->clear();

  // Test case, where events are out of order
  fuchsia::debugdata::DebugDataVmoTokenPtr token_4, token_5, token_6;
  foo_bar_ptr = SendCapabilityRequestedEvent(event_stream_ptr, "foo/bar");
  foo_bar_ptr->Publish("foo_bar_sink1", GetVmo(), token_4.NewRequest());
  foo_bar_ptr->Publish("foo_bar_sink2", GetVmo(), token_5.NewRequest());
  foo_bar_ptr->Publish("foo_bar_sink3", GetVmo(), token_6.NewRequest());

  RunLoopUntilIdle();
  // as we have not closed the conenction, we should not get the VMOs
  ASSERT_EQ(shared_vec->size(), 0u);

  SendComponentStopEvent(event_stream_ptr, "foo/bar");
  RunLoopUntilIdle();
  ASSERT_EQ(shared_vec->size(), 1u);
  ASSERT_EQ(shared_vec->at(0).second.size(), 3u);
  ASSERT_EQ(shared_vec->at(0).first, "foo_bar.cm");

  // send publish after stop was sent
  fuchsia::debugdata::DebugDataVmoTokenPtr token_7, token_8;
  foo_bar_ptr->Publish("foo_bar_sink1", GetVmo(), token_7.NewRequest());
  foo_bar_ptr->Publish("foo_bar_sink2", GetVmo(), token_8.NewRequest());
  foo_bar_ptr.Unbind();

  // make sure we get those VMOs.
  RunLoopUntilIdle();
  ASSERT_EQ(shared_vec->size(), 2u);
  ASSERT_EQ(shared_vec->at(1).second.size(), 2u);
  ASSERT_EQ(shared_vec->at(1).first, "foo_bar.cm");
  shared_vec->clear();

  // send events on multiple monikers
  size_t const SIZE = 4;
  std::string monikers[SIZE] = {"foo/bar", "foo", "bar/foo", "some/moniker"};
  fidl::InterfacePtr<fuchsia::debugdata::DebugData> ptrs[SIZE];
  for (size_t i = 0; i < SIZE; i++) {
    ptrs[i] = SendCapabilityRequestedEvent(event_stream_ptr, monikers[i]);
  }

  shared_vec->clear();
  std::map<std::string, size_t> count = {
      {monikers[0], 2}, {monikers[1], 4}, {monikers[2], 3}, {monikers[3], 5}};
  for (size_t i = 0; i < SIZE; i++) {
    for (size_t j = 0; j < count[monikers[i]]; j++) {
      auto data_sink = fxl::StringPrintf("ds_%zu_%zu", i, j);
      fuchsia::debugdata::DebugDataVmoTokenPtr token;
      ptrs[i]->Publish(data_sink, GetVmo(), token.NewRequest());
    }
  }
  RunLoopUntilIdle();
  ASSERT_EQ(shared_vec->size(), 0u);

  for (auto& moniker : monikers) {
    SendComponentStopEvent(event_stream_ptr, moniker);
  }

  RunLoopUntilIdle();
  ASSERT_EQ(shared_vec->size(), SIZE);
  // add url->moniker mapping to map
  std::map<std::string, std::string> url_moniker_map;
  for (auto& x : moniker_url_map) {
    url_moniker_map[x.second] = x.first;
  }
  url_moniker_map[""] = "some/moniker";

  for (size_t i = 0; i < SIZE; i++) {
    auto moniker = url_moniker_map[shared_vec->at(i).first];
    ASSERT_EQ(shared_vec->at(i).second.size(), count[moniker]) << shared_vec->at(i).first;
  }

  shared_vec->clear();
}
