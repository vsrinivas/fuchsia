// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_TEST_MANAGER_DEBUG_DATA_PROCESSOR_TEST_DATA_PROCESSOR_H_
#define SRC_SYS_TEST_MANAGER_DEBUG_DATA_PROCESSOR_TEST_DATA_PROCESSOR_H_

#include <lib/zx/event.h>

#include <map>
#include <string>

#include "abstract_data_processor.h"
#include "common.h"

class TestDataProcessor : public AbstractDataProcessor {
 public:
  using UrlDataMap = std::map<std::string, std::vector<DataSinkDump>>;
  explicit TestDataProcessor(std::shared_ptr<UrlDataMap> map) : map_(std::move(map)) {
    zx::event::create(0, &idle_signal_event_);
    // Since we don't actually process anything, it's okay to leave this always idle.
    // This works so long as there's only one thread calling ProcessData and checking for
    // idle.
    idle_signal_event_.signal(0, IDLE_SIGNAL);
  }

  /// Create a test data processor for testing the idle signal.
  TestDataProcessor(zx_handle_t idle_signal) : idle_signal_event_(idle_signal) {
    map_ = std::make_shared<UrlDataMap>();
  }

  void ProcessData(std::string test_url, DataSinkDump data_sink) override {
    if (map_->find(test_url) == map_->end()) {
      std::vector<DataSinkDump> data_sink_vec;
      map_->emplace(test_url, std::move(data_sink_vec));
    }
    map_->at(test_url).push_back(std::move(data_sink));
  }

  zx::unowned_event GetIdleEvent() override { return idle_signal_event_.borrow(); }

  std::shared_ptr<UrlDataMap> map_;
  zx::event idle_signal_event_;
};

#endif  // SRC_SYS_TEST_MANAGER_DEBUG_DATA_PROCESSOR_TEST_DATA_PROCESSOR_H_
