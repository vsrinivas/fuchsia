// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_TEST_MANAGER_DEBUG_DATA_PROCESSOR_DATA_PROCESSOR_H_
#define SRC_SYS_TEST_MANAGER_DEBUG_DATA_PROCESSOR_DATA_PROCESSOR_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/dispatcher.h>
#include <lib/zx/event.h>

#include <map>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <fbl/unique_fd.h>

#include "abstract_data_processor.h"
#include "common.h"
#include "src/lib/fxl/synchronization/thread_annotations.h"

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

// Signal indicating more data is ready to be processed.
const uint32_t PENDING_DATA_SIGNAL = ZX_USER_SIGNAL_1;
static_assert(IDLE_SIGNAL != PENDING_DATA_SIGNAL);

class DataProcessor : public AbstractDataProcessor {
 public:
  /// dir_fd: directory to write processed debug data to.
  explicit DataProcessor(fbl::unique_fd dir_fd, async_dispatcher_t* dispatcher);
  ~DataProcessor() override;

  /// Process data on internal dispacther.
  void ProcessData(std::string test_url, DataSinkDump data_sink) override;

  zx::unowned_event GetIdleEvent() override;

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
  static void WriteSummaryFile(fbl::unique_fd& fd, TestDebugDataMap debug_data_map);

 private:
  /// Container for information shared across threads.
  class DataProcessorInner {
   public:
    explicit DataProcessorInner(fbl::unique_fd dir_fd);

    TestSinkMap TakeMapContents();
    void AddData(std::string test_url, DataSinkDump data_sink_dump);
    void SignalIdleIfEmpty();
    zx::unowned_event GetEvent();
    fbl::unique_fd& GetFd();

   private:
    std::mutex mutex_;
    TestSinkMap data_sink_map_ FXL_GUARDED_BY(mutex_);
    zx::event idle_signal_event_;
    fbl::unique_fd dir_fd_;
  };

  /// Process data present in `data_sink_map_`.
  static void ProcessDataInner(std::shared_ptr<DataProcessorInner> inner);

  std::shared_ptr<DataProcessorInner> inner_;
  async_dispatcher_t* dispatcher_;
  // This Wait is used to schedule work in the thread for dispatcher_ and should
  // only be accessed from that thread.
  std::shared_ptr<async::Wait> processor_wait_;
};

#endif  // SRC_SYS_TEST_MANAGER_DEBUG_DATA_PROCESSOR_DATA_PROCESSOR_H_
