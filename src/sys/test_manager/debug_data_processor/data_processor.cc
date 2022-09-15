// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "data_processor.h"

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/debugdata/datasink.h>
#include <lib/syslog/cpp/macros.h>
#include <string.h>
#include <zircon/assert.h>

#include <unordered_map>
#include <vector>

#include "rapidjson/rapidjson.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "src/lib/files/file.h"
#include "src/lib/json_parser/json_parser.h"

namespace {
const std::string kSummaryFile = "summary.json";
}

DataProcessor::DataProcessor(fbl::unique_fd dir_fd, async_dispatcher_t* dispatcher)
    : dispatcher_(dispatcher) {
  FX_CHECK(dispatcher_ != nullptr);
  inner_ = std::make_shared<DataProcessorInner>();
  std::weak_ptr<DataProcessorInner> weak_inner = inner_;
  auto idle_event = inner_->GetEvent();

  auto state = std::make_shared<ProcessorState>(std::move(dir_fd));

  processor_wait_ =
      std::make_shared<async::Wait>(idle_event->get(), STATE_DIRTY_SIGNAL, 0,
                                    [weak_inner = std::move(weak_inner), state = std::move(state)](
                                        async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                        zx_status_t status, const zx_packet_signal_t* signal) {
                                      // Terminate if the wait was cancelled.
                                      if (status != ZX_OK) {
                                        return;
                                      }
                                      auto owned = weak_inner.lock();
                                      // Terminate if the processor no longer exists.
                                      if (!owned) {
                                        return;
                                      }
                                      if (!ProcessDataInner(owned, state)) {
                                        wait->Begin(dispatcher);
                                      }
                                    });

  // async::Wait is not threadsafe, so to ensure only the processor thread accesses it,
  // we post a task to the processor thread, which in turn beings the wait.
  async::PostTask(dispatcher_, [processor_wait = processor_wait_, dispatcher = dispatcher_]() {
    FX_CHECK(processor_wait->Begin(dispatcher) == ZX_OK);
  });
}

DataProcessor::~DataProcessor() {
  // async::Wait is not threadsafe. Since we delegate processing to the processor
  // thread, post a task to handle it's destruction there. The handle used to wait on the
  // signal must still be valid when Cancel is called, so we move inner_ (which contains
  // the handle) to the processor thread.
  async::PostTask(dispatcher_, [processor_wait = std::move(processor_wait_),
                                inner = std::move(inner_)] { processor_wait->Cancel(); });
}

void DataProcessor::ProcessData(std::string test_url, DataSinkDump data_sink_dump) {
  inner_->AddData(std::move(test_url), std::move(data_sink_dump));
}

void DataProcessor::FinishProcessing() { inner_->FinishProcessing(); }

zx::unowned_event DataProcessor::GetDataFlushedEvent() { return inner_->GetEvent(); }

DataProcessor::DataProcessorInner::DataProcessorInner() : done_adding_data_(false) {
  FX_CHECK(zx::event::create(0, &idle_signal_event_) == ZX_OK);
  idle_signal_event_.signal(STATE_DIRTY_SIGNAL | DATA_FLUSHED_SIGNAL, 0);
}

TestSinkMap DataProcessor::DataProcessorInner::TakeMapContents() {
  mutex_.lock();
  auto result = std::move(data_sink_map_);
  data_sink_map_ = TestSinkMap();
  // Clear dirty bit once we take data.
  idle_signal_event_.signal(STATE_DIRTY_SIGNAL, 0);
  mutex_.unlock();
  return result;
}

void DataProcessor::DataProcessorInner::AddData(std::string test_url, DataSinkDump data_sink_dump) {
  mutex_.lock();
  ZX_ASSERT(!done_adding_data_);
  auto& map = data_sink_map_[std::move(test_url)];
  map[std::move(data_sink_dump.data_sink)].push_back(std::move(data_sink_dump.vmo));
  idle_signal_event_.signal(0, STATE_DIRTY_SIGNAL);
  mutex_.unlock();
}

void DataProcessor::DataProcessorInner::FinishProcessing() {
  ZX_ASSERT(!DataFinished());
  mutex_.lock();
  done_adding_data_ = true;
  idle_signal_event_.signal(0, STATE_DIRTY_SIGNAL);
  mutex_.unlock();
}

bool DataProcessor::DataProcessorInner::DataFinished() {
  mutex_.lock();
  bool complete = data_sink_map_.empty() && done_adding_data_;
  mutex_.unlock();
  return complete;
}

void DataProcessor::DataProcessorInner::SignalFlushed() {
  idle_signal_event_.signal(0, DATA_FLUSHED_SIGNAL);
}

zx::unowned_event DataProcessor::DataProcessorInner::GetEvent() {
  return idle_signal_event_.borrow();
}

bool DataProcessor::ProcessDataInner(std::shared_ptr<DataProcessorInner> inner,
                                     std::shared_ptr<ProcessorState> state) {
  auto data_sink_map = inner->TakeMapContents();
  for (auto& [test_url, sink_vmo_map] : data_sink_map) {
    debugdata::DataSinkCallback error_log_fn = [&](const std::string& error) {
      FX_LOGS(ERROR) << "ProcessDebugData: " << error;
    };
    debugdata::DataSinkCallback warn_log_fn = [&](const std::string& warning) {
      FX_LOGS(WARNING) << "ProcessDebugData: " << warning;
    };
    for (auto& [data_sink_name, vmos] : sink_vmo_map) {
      for (auto& vmo : vmos) {
        state->data_sink.ProcessSingleDebugData(data_sink_name, std::move(vmo), test_url,
                                                error_log_fn, warn_log_fn);
      }
    }
  }

  if (inner->DataFinished()) {
    debugdata::DataSinkCallback error_log_fn = [&](const std::string& error) {
      FX_LOGS(ERROR) << "FlushDebugData: " << error;
    };
    debugdata::DataSinkCallback warn_log_fn = [&](const std::string& warning) {
      FX_LOGS(WARNING) << "FlushDebugData: " << warning;
    };
    auto sinks_map = state->data_sink.FlushToDirectory(error_log_fn, warn_log_fn);

    TestDebugDataMap debug_data_map;
    for (auto& [sink_name, dump_file_tag_map] : sinks_map) {
      for (auto& [dump_file, urls] : dump_file_tag_map) {
        for (auto url : urls) {
          debug_data_map[url][sink_name][dump_file.file] = dump_file.name;
        }
      }
    }

    WriteSummaryFile(state->dir_fd, debug_data_map);
    inner->SignalFlushed();
    return true;
  }
  return false;
}

namespace {
const char* kSummaryTests = "tests";
const char* kSummaryTestName = "name";
const char* kSummaryDataSinks = "data_sinks";
const char* kSummaryDataSinkName = "name";
const char* kSummaryDataSinkFile = "file";

}  // namespace

void DataProcessor::WriteSummaryFile(fbl::unique_fd& fd, TestDebugDataMap debug_data_map) {
  // load current summary.json file and load it into our map.
  if (std::string current_summary;
      files::ReadFileToStringAt(fd.get(), kSummaryFile, &current_summary)) {
    json_parser::JSONParser parser;
    auto doc = parser.ParseFromString(current_summary, kSummaryFile);
    FX_CHECK(!parser.HasError()) << "can't parse summary.json: " << parser.error_str().c_str();
    if (doc.HasMember(kSummaryTests)) {
      const auto& tests = doc[kSummaryTests].GetArray();
      for (const auto& test : tests) {
        FX_CHECK(test.HasMember(kSummaryTestName));
        auto url = test[kSummaryTestName].GetString();
        if (test.HasMember(kSummaryDataSinks)) {
          auto& map = debug_data_map[url];
          const auto& debug_data_map = test[kSummaryDataSinks].GetObject();
          for (auto& entry : debug_data_map) {
            auto& data_map = map[entry.name.GetString()];
            const auto& data_entries = entry.value.GetArray();
            for (auto& data_entry : data_entries) {
              // override the value in map with value in summary.json.
              data_map[data_entry[kSummaryDataSinkFile].GetString()] =
                  data_entry[kSummaryDataSinkName].GetString();
            }
          }
        }
      }
    }
  }

  // write our map to file
  rapidjson::Document doc;
  doc.SetObject();
  auto& allocator = doc.GetAllocator();
  auto tests = rapidjson::Value(rapidjson::kArrayType);

  for (auto& [test_url, data_sink_map] : debug_data_map) {
    auto test = rapidjson::Value(rapidjson::kObjectType);
    test.AddMember(rapidjson::Value(kSummaryTestName, allocator).Move(),
                   rapidjson::Value(test_url, allocator).Move(), allocator);
    auto debug_data_map = rapidjson::Value(rapidjson::kObjectType);
    for (auto& [data_sink_name, dump_file_map] : data_sink_map) {
      auto dump_files = rapidjson::Value(rapidjson::kArrayType);
      for (auto& [file, name] : dump_file_map) {
        auto dump_file = rapidjson::Value(rapidjson::kObjectType);
        dump_file.AddMember(rapidjson::Value(kSummaryDataSinkFile, allocator).Move(),
                            rapidjson::Value(file, allocator).Move(), allocator);
        dump_file.AddMember(rapidjson::Value(kSummaryDataSinkName, allocator).Move(),
                            rapidjson::Value(name, allocator).Move(), allocator);
        dump_files.PushBack(dump_file.Move(), allocator);
      }
      debug_data_map.AddMember(rapidjson::Value(data_sink_name, allocator).Move(),
                               dump_files.Move(), allocator);
    }
    test.AddMember(rapidjson::Value(kSummaryDataSinks, allocator).Move(), debug_data_map.Move(),
                   allocator);
    tests.PushBack(test.Move(), allocator);
  }
  doc.AddMember(rapidjson::Value(kSummaryTests, allocator).Move(), tests.Move(), allocator);

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  FX_CHECK(files::WriteFileAt(fd.get(), kSummaryFile, buffer.GetString(), buffer.GetSize()))
      << strerror(errno);
}
