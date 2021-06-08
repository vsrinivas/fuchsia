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
#include "test_data_processor.h"

using EventStreamTest = gtest::RealLoopFixture;

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

zx::vmo GetVmo() {
  zx::vmo vmo;
  zx::vmo::create(100, 0, &vmo);
  return vmo;
}

TEST_F(EventStreamTest, Test) {
  auto shared_map = std::make_shared<TestDataProcessor::UrlDataMap>();
  std::unique_ptr<AbstractDataProcessor> data_sink_processor =
      std::make_unique<TestDataProcessor>(shared_map);
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
  // As we have not closed the controllers, we should not get the VMOs
  ASSERT_EQ(shared_map->size(), 0u);
  foo_bar_ptr.Unbind();
  RunLoopUntilIdle();
  ASSERT_EQ(shared_map->size(), 0u);

  // After closing controller channels, we should get the VMOs
  token_1.Unbind();
  token_2.Unbind();
  token_3.Unbind();
  RunLoopUntilIdle();
  ASSERT_EQ(shared_map->size(), 1u);
  ASSERT_EQ(shared_map->at("foo_bar.cm").size(), 3u);
  shared_map->clear();

  // send events on multiple monikers
  size_t const SIZE = 4;
  std::string monikers[SIZE] = {"foo/bar", "foo", "bar/foo", "some/moniker"};
  fidl::InterfacePtr<fuchsia::debugdata::DebugData> ptrs[SIZE];
  for (size_t i = 0; i < SIZE; i++) {
    ptrs[i] = SendCapabilityRequestedEvent(event_stream_ptr, monikers[i]);
  }

  std::map<std::string, size_t> count = {
      {monikers[0], 2}, {monikers[1], 4}, {monikers[2], 3}, {monikers[3], 5}};
  for (size_t i = 0; i < SIZE; i++) {
    for (size_t j = 0; j < count[monikers[i]]; j++) {
      auto data_sink = fxl::StringPrintf("ds_%zu_%zu", i, j);
      fuchsia::debugdata::DebugDataVmoTokenPtr token;
      ptrs[i]->Publish(data_sink, GetVmo(), token.NewRequest());
      // tokens for these vmos are closed here.
    }
  }

  RunLoopUntilIdle();
  ASSERT_EQ(shared_map->size(), SIZE);
  // add url->moniker mapping to map
  std::map<std::string, std::string> url_moniker_map;
  for (auto& x : moniker_url_map) {
    url_moniker_map[x.second] = x.first;
  }
  url_moniker_map[""] = "some/moniker";

  for (auto& entry : *shared_map) {
    auto moniker = url_moniker_map[entry.first];
    ASSERT_EQ(entry.second.size(), count[moniker]) << moniker;
  }

  shared_map->clear();
}
