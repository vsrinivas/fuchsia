// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_TEST_MANAGER_DEBUG_DATA_DATA_PROCESSOR_H_
#define SRC_SYS_TEST_MANAGER_DEBUG_DATA_DATA_PROCESSOR_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/dispatcher.h>

#include <map>
#include <unordered_map>
#include <vector>

#include <fbl/unique_fd.h>

#include "abstract_data_processor.h"
#include "common.h"

// represent debugdata::DumpFile as a map
using DumpFileMap = std::map<std::string, std::string>;

// key = data_sink name
using DataSinkMap = std::map<std::string, DumpFileMap>;

struct TestDebugDataMapValue {
  /// processing of debug data passed without error.
  bool data_processing_passed;

  DataSinkMap data_sink_map;
};

// key = test url
using TestDebugDataMap = std::map<std::string, TestDebugDataMapValue>;

// key = sink name, value = vector of VMOs published to debug data.
using SinkVMOMap = std::unordered_map<std::string, std::vector<zx::vmo>>;

// key = test_url
using TestSinkMap = std::map<std::string, SinkVMOMap>;

/// This class is not thread safe.
class DataProcessor : public AbstractDataProcessor {
 public:
  /// dir_fd: directory to write processed debug data to.
  explicit DataProcessor(fbl::unique_fd dir_fd, async_dispatcher_t* dispatcher);
  ~DataProcessor() override;

  /// Process data on internal dispacther.
  void ProcessData(std::string test_url, DataSinkDump data_sink) override;

  /// Loads current summary.json if available, merges it with the passed map and writes the map back
  /// to summary.json.
  /// If current summary.json is corrupted, this function will crash to catch bugs early.
  /// The format of summary.json is same as used in //zircon/system/ulib/runtest-utils.
  /// Sample format:
  /// {
  ///   "tests":[
  ///      {
  ///         "name":"test_url1.cmx",
  ///         "result":"PASS",
  ///         "data_sinks":{
  ///            "test1_sink1":[
  ///               {
  ///                  "file":"path/path1",
  ///                  "name":"name1"
  ///               },
  ///               {
  ///                  "file":"path/path1_1",
  ///                  "name":"name1_1"
  ///               }
  ///            ],
  ///            "test1_sink2":[
  ///               {
  ///                  "file":"path/path2",
  ///                  "name":"name2"
  ///               },
  ///            ],
  ///           ...
  ///         }
  ///      },
  ///     ...
  ///   ]//
  /// }
  void WriteSummaryFile(TestDebugDataMap debug_data_map);

 private:
  /// Add to `data_sink_map_` and schedule a call to `ProcessDataInner`
  void Add(std::string test_url, DataSinkDump data_sink_vec);

  /// Process data present in `data_sink_map_`.
  void ProcessDataInner();

  TestSinkMap data_sink_map_;
  fbl::unique_fd dir_fd_;
  async_dispatcher_t* dispatcher_;
};

#endif  // SRC_SYS_TEST_MANAGER_DEBUG_DATA_DATA_PROCESSOR_H_
