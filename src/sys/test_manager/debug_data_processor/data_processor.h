// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_TEST_MANAGER_DEBUG_DATA_PROCESSOR_DATA_PROCESSOR_H_
#define SRC_SYS_TEST_MANAGER_DEBUG_DATA_PROCESSOR_DATA_PROCESSOR_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/dispatcher.h>
#include <lib/debugdata/datasink.h>
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

// key = test url
using TestDebugDataMap = std::map<std::string, DataSinkMap>;

// key = sink name, value = vector of VMOs published to debug data.
using SinkVMOMap = std::unordered_map<std::string, std::vector<zx::vmo>>;

// key = test_url
using TestSinkMap = std::map<std::string, SinkVMOMap>;

// Signal indicating more data has been added or marked complete.
const uint32_t STATE_DIRTY_SIGNAL = ZX_USER_SIGNAL_1;
static_assert(STATE_DIRTY_SIGNAL != DATA_FLUSHED_SIGNAL);

class DataProcessor : public AbstractDataProcessor {
 public:
  /// dir_fd: directory to write processed debug data to.
  explicit DataProcessor(fbl::unique_fd dir_fd, async_dispatcher_t* dispatcher);
  ~DataProcessor() override;

  /// Process data on internal dispacther.
  void ProcessData(std::string test_url, DataSinkDump data_sink) override;

  void FinishProcessing() override;

  zx::unowned_event GetDataFlushedEvent() override;

  /// Loads current summary.json if available, merges it with the passed map and writes the map back
  /// to summary.json.
  /// If current summary.json is corrupted, this function will crash to catch bugs early.
  /// The format of summary.json is same as used in //zircon/system/ulib/runtest-utils.
  /// Sample format:
  /// {
  ///   "tests":[
  ///      {
  ///         "name":"test_url1.cmx",
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
    explicit DataProcessorInner();

    TestSinkMap TakeMapContents();
    void AddData(std::string test_url, DataSinkDump data_sink_dump);
    void FinishProcessing();
    bool DataFinished();
    void SignalFlushed();
    zx::unowned_event GetEvent();

   private:
    std::mutex mutex_;
    TestSinkMap data_sink_map_ FXL_GUARDED_BY(mutex_);
    bool done_adding_data_ FXL_GUARDED_BY(mutex_);
    zx::event idle_signal_event_;
  };

  /// State only accessed by the processing thread.
  struct ProcessorState {
    fbl::unique_fd dir_fd;
    debugdata::DataSink data_sink;

    explicit ProcessorState(fbl::unique_fd arg_dir_fd)
        : dir_fd(std::move(arg_dir_fd)), data_sink(dir_fd) {}
  };

  /// Process data present in `data_sink_map_`. Returns true iff processing
  /// all data is complete.
  static bool ProcessDataInner(std::shared_ptr<DataProcessorInner> inner,
                               std::shared_ptr<ProcessorState> state);

  std::shared_ptr<DataProcessorInner> inner_;
  async_dispatcher_t* dispatcher_;
  // This Wait is used to schedule work in the thread for dispatcher_ and should
  // only be accessed from that thread.
  std::shared_ptr<async::Wait> processor_wait_;
};

#endif  // SRC_SYS_TEST_MANAGER_DEBUG_DATA_PROCESSOR_DATA_PROCESSOR_H_
