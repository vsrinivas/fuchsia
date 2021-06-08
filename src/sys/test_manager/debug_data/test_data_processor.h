// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_TEST_MANAGER_DEBUG_DATA_TEST_DATA_PROCESSOR_H_
#define SRC_SYS_TEST_MANAGER_DEBUG_DATA_TEST_DATA_PROCESSOR_H_

#include <map>
#include <string>

#include "abstract_data_processor.h"
#include "common.h"

class TestDataProcessor : public AbstractDataProcessor {
 public:
  using UrlDataMap = std::map<std::string, std::vector<DataSinkDump>>;
  explicit TestDataProcessor(std::shared_ptr<UrlDataMap> map) : map_(std::move(map)) {}

  void ProcessData(std::string test_url, DataSinkDump data_sink) override {
    if (map_->find(test_url) == map_->end()) {
      std::vector<DataSinkDump> data_sink_vec;
      map_->emplace(test_url, std::move(data_sink_vec));
    }
    map_->at(test_url).push_back(std::move(data_sink));
  }

  std::shared_ptr<UrlDataMap> map_;
};

#endif  // SRC_SYS_TEST_MANAGER_DEBUG_DATA_TEST_DATA_PROCESSOR_H_
