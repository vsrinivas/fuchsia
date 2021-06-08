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
    : dir_fd_(std::move(dir_fd)), dispatcher_(dispatcher) {
  FX_CHECK(dispatcher_ != nullptr);
}

DataProcessor::~DataProcessor() = default;

void DataProcessor::ProcessData(std::string test_url, DataSinkDump data_sink_dump) {
  // we want to run below task on local loop.

  async::PostTask(dispatcher_, [this, test_url = std::move(test_url),
                                data_sink_dump = std::move(data_sink_dump)]() mutable {
    Add(std::move(test_url), std::move(data_sink_dump));
  });
}

void DataProcessor::Add(std::string test_url, DataSinkDump data_sink_dump) {
  auto schedule_processor = data_sink_map_.empty();
  auto& map = data_sink_map_[std::move(test_url)];
  map[std::move(data_sink_dump.data_sink)].push_back(std::move(data_sink_dump.vmo));

  if (schedule_processor) {
    async::PostTask(dispatcher_, [this]() { ProcessDataInner(); });
  }
}

void DataProcessor::ProcessDataInner() {
  auto data_sink_map = std::move(data_sink_map_);
  data_sink_map_ = TestSinkMap();
  TestDebugDataMap debug_data_map;
  for (auto& [test_url, sink_vmo_map] : data_sink_map) {
    auto url = test_url;
    bool got_error = false;
    auto sinks_map = debugdata::ProcessDebugData(
        dir_fd_, std::move(sink_vmo_map),
        [&](const std::string& error) {
          FX_LOGS(ERROR) << "ProcessDebugData: " << error;
          got_error = true;
        },
        [&](const std::string& warning) { FX_LOGS(WARNING) << "ProcessDebugData: " << warning; });
    auto& debug_data_entry = debug_data_map[std::move(url)];
    debug_data_entry.data_processing_passed = !got_error;

    for (auto& [sink_name, dump_files] : sinks_map) {
      auto& file_map = debug_data_entry.data_sink_map[sink_name];
      for (auto& e : dump_files) {
        file_map[std::move(e.file)] = std::move(e.name);
      }
    }
  }
  WriteSummaryFile(std::move(debug_data_map));
}

namespace {
const char* kSummaryTests = "tests";
const char* kSummaryTestName = "name";
const char* kSummaryResult = "result";
const char* kSummaryDataSinks = "data_sinks";
const char* kSummaryDataSinkName = "name";
const char* kSummaryDataSinkFile = "file";
const std::string kSummaryResultFail = "FAIL";
const std::string kSummaryResultPass = "PASS";

}  // namespace

void DataProcessor::WriteSummaryFile(TestDebugDataMap debug_data_map) {
  // load current summary.json file and load it into our map.
  if (std::string current_summary;
      files::ReadFileToStringAt(dir_fd_.get(), kSummaryFile, &current_summary)) {
    json_parser::JSONParser parser;
    auto doc = parser.ParseFromString(current_summary, kSummaryFile);
    FX_CHECK(!parser.HasError()) << "can't parse summary.json: " << parser.error_str().c_str();
    if (doc.HasMember(kSummaryTests)) {
      const auto& tests = doc[kSummaryTests].GetArray();
      for (const auto& test : tests) {
        FX_CHECK(test.HasMember(kSummaryTestName));
        auto url = test[kSummaryTestName].GetString();
        FX_CHECK(test.HasMember(kSummaryResult));
        std::string result = test[kSummaryResult].GetString();
        debug_data_map[url].data_processing_passed = true;
        if (result == kSummaryResultFail) {
          debug_data_map[url].data_processing_passed = false;
        }
        if (test.HasMember(kSummaryDataSinks)) {
          auto& map = debug_data_map[url].data_sink_map;
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

  for (auto& [test_url, data_sink_map_value] : debug_data_map) {
    auto test = rapidjson::Value(rapidjson::kObjectType);
    test.AddMember(rapidjson::Value(kSummaryTestName, allocator).Move(),
                   rapidjson::Value(test_url, allocator).Move(), allocator);
    auto result =
        data_sink_map_value.data_processing_passed ? kSummaryResultPass : kSummaryResultFail;
    test.AddMember(rapidjson::Value(kSummaryResult, allocator).Move(),
                   rapidjson::Value(result, allocator).Move(), allocator);
    auto debug_data_map = rapidjson::Value(rapidjson::kObjectType);
    for (auto& [data_sink_name, dump_file_map] : data_sink_map_value.data_sink_map) {
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

  FX_CHECK(files::WriteFileAt(dir_fd_.get(), kSummaryFile, buffer.GetString(), buffer.GetSize()))
      << strerror(errno);
}
