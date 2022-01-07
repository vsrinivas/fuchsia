// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/fidl/cpp/interface_ptr.h>

#include <memory>
#include <optional>
#include <utility>

#include <gtest/gtest.h>
#include <src/lib/fxl/strings/string_printf.h>
#include <src/lib/testing/loop_fixture/real_loop_fixture.h>

#include "abstract_data_processor.h"
#include "data_processor.h"
#include "src/lib/files/file.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/json_parser/json_parser.h"

class WriteSummaryFileTest : public gtest::RealLoopFixture {
 public:
  WriteSummaryFileTest() {
    tmp_fd_ = open("/tmp", O_DIRECTORY | O_RDWR);
    ZX_ASSERT_MSG(tmp_fd_ != -1, "/tmp: %s", strerror(errno));

    temp_dir_ = std::make_unique<files::ScopedTempDirAt>(tmp_fd_);
    processor_ = std::make_unique<DataProcessor>(GetTempDirFd(), dispatcher());
  }

  fbl::unique_fd GetTempDirFd() {
    int fd = openat(tmp_fd_, temp_dir_->path().c_str(), O_DIRECTORY | O_RDWR);
    ZX_ASSERT_MSG(fd != -1, "%s: %s", temp_dir_->path().c_str(), strerror(errno));
    return fbl::unique_fd(fd);
  }

  std::unique_ptr<DataProcessor>& processor() { return processor_; }

 private:
  int tmp_fd_;
  std::unique_ptr<files::ScopedTempDirAt> temp_dir_;
  std::unique_ptr<DataProcessor> processor_;
};

namespace {
const std::string kSummaryFile = "summary.json";
}

TEST_F(WriteSummaryFileTest, EmptyInput) {
  auto fd = GetTempDirFd();
  processor()->WriteSummaryFile(fd, TestDebugDataMap());
  std::string result;

  files::ReadFileToStringAt(fd.get(), kSummaryFile, &result);
  ASSERT_EQ(result, "{\"tests\":[]}");
}

namespace {

void AssertSummaryJson(const TestDebugDataMap& map, fbl::unique_fd tmp_fd_) {
  std::string result;
  ASSERT_TRUE(files::ReadFileToStringAt(tmp_fd_.get(), kSummaryFile, &result));

  json::JSONParser parser;
  auto doc = parser.ParseFromString(result, "");
  ASSERT_FALSE(parser.HasError()) << parser.error_str();

  ASSERT_TRUE(doc.HasMember("tests"));
  const auto& tests = doc["tests"].GetArray();

  ASSERT_EQ(tests.Size(), map.size());
  for (auto& test : tests) {
    auto test_name = test["name"].GetString();
    auto it = map.find(test_name);
    ASSERT_NE(it, map.end()) << test_name;
    ASSERT_STREQ(test["result"].GetString(), it->second.data_processing_passed ? "PASS" : "FAIL")
        << test_name;

    const auto& data_sinks = test["data_sinks"].GetObject();
    auto& data_sink_map = it->second.data_sink_map;
    ASSERT_EQ(data_sinks.MemberCount(), data_sink_map.size());
    for (auto& entry : data_sink_map) {
      ASSERT_TRUE(data_sinks.HasMember(entry.first.c_str()));
      const auto& sink = data_sinks[entry.first.c_str()].GetArray();
      ASSERT_EQ(sink.Size(), entry.second.size());
      for (auto& e : sink) {
        auto file = e["file"].GetString();
        auto it = entry.second.find(file);
        ASSERT_NE(it, entry.second.end()) << file;
        ASSERT_EQ(e["name"].GetString(), it->second);
      }
    }
  }
}

}  // namespace

TEST_F(WriteSummaryFileTest, OneTestWithMultipleDataSinks) {
  TestDebugDataMap map;
  DataSinkMap data_sink_map;

  data_sink_map["sink1"] = {{"path/path1", "path1"}, {"path/path1_1", "path1_1"}};
  data_sink_map["sink2"] = {{"path/path2_0", "path2_0"}, {"path/path2", "path2"}};
  data_sink_map["sink3"] = {{"path/path3", "path3"}};

  map["simple_test.cmx"] = TestDebugDataMapValue{true, std::move(data_sink_map)};

  auto fd = GetTempDirFd();
  processor()->WriteSummaryFile(fd, map);
  AssertSummaryJson(map, std::move(fd));
  ASSERT_FALSE(testing::Test::HasFailure());
}

TEST_F(WriteSummaryFileTest, MultipleTestWithMultipleDataSinks) {
  TestDebugDataMap map;
  DataSinkMap data_sink_map;
  data_sink_map["test1_sink1"] = {{"path/path1", "path1"}, {"path/path1_1", "path1_1"}};
  data_sink_map["test1_sink2"] = {{"path/path2_0", "path2_0"}, {"path/path2", "path2"}};
  data_sink_map["test1_sink3"] = {{"path/path3", "path3"}};

  map["test_url1.cmx"] = TestDebugDataMapValue{true, std::move(data_sink_map)};

  data_sink_map = DataSinkMap();
  data_sink_map["sink1"] = {{"path/path1", "path1"}, {"path/path1_1", "path1_1"}};
  data_sink_map["sink2"] = {{"path/path2_0", "path2_0"}, {"path/path2", "path2"}};
  data_sink_map["sink3"] = {{"path/path3", "path3"}};

  map["test_url2.cmx"] = TestDebugDataMapValue{true, std::move(data_sink_map)};

  data_sink_map = DataSinkMap();
  data_sink_map["test3_sink1"] = {{"path/path1", "path1"}, {"path/path1_1", "path1_1"}};
  data_sink_map["test3_sink2"] = {{"path/path2_0", "path2_0"}, {"path/path2", "path2"}};
  data_sink_map["test3_sink3"] = {{"path/path3", "path3"}};

  map["test_url3.cmx"] = TestDebugDataMapValue{false, std::move(data_sink_map)};

  data_sink_map = DataSinkMap();
  data_sink_map["sink1"] = {{"path/path1", "path1"}, {"path/path1_1", "path1_1"}};
  data_sink_map["sink2"] = {{"path/path2_0", "path2_0"}, {"path/path2", "path2"}};
  data_sink_map["sink3"] = {{"path/path3", "path3"}};

  map["test_url4.cmx"] = TestDebugDataMapValue{false, std::move(data_sink_map)};

  auto fd = GetTempDirFd();
  processor()->WriteSummaryFile(fd, map);
  AssertSummaryJson(map, std::move(fd));
  ASSERT_FALSE(testing::Test::HasFailure());
}

TEST_F(WriteSummaryFileTest, MultipleWriteSummaryFileCalls) {
  TestDebugDataMap map;
  DataSinkMap data_sink_map;
  data_sink_map["sink1"] = {{"path/path1", "path1"}, {"path/path1_1", "path1_1"}};
  data_sink_map["sink2"] = {{"path/path2_0", "path2_0"}, {"path/path2", "path2"}};
  data_sink_map["sink3"] = {{"path/path3", "path3"}};

  map["test_url1.cmx"] = TestDebugDataMapValue{false, std::move(data_sink_map)};

  data_sink_map = DataSinkMap();
  data_sink_map["sink1"] = {{"path/path1", "path1"}, {"path/path1_1", "path1_1"}};
  data_sink_map["sink2"] = {{"path/path2_0", "path2_0"}, {"path/path2", "path2"}};
  data_sink_map["sink3"] = {{"path/path3", "path3"}};

  map["test_url2.cmx"] = TestDebugDataMapValue{true, std::move(data_sink_map)};

  // test that reading empty tets url doesn't lead to crash
  data_sink_map = DataSinkMap();
  data_sink_map["sink1"] = {{"path/path1", "path1"}, {"path/path1_1", "path1_1"}};
  data_sink_map["sink2"] = {{"path/path2_0", "path2_0"}, {"path/path2", "path2"}};
  data_sink_map["sink3"] = {{"path/path3", "path3"}};

  map[""] = TestDebugDataMapValue{true, std::move(data_sink_map)};

  auto fd = GetTempDirFd();
  processor()->WriteSummaryFile(fd, map);

  TestDebugDataMap new_map;
  data_sink_map["sink1"] = {{"path/path1", "path1"}, {"path/path1_2", "path1_2"}};  // add new sink
  data_sink_map["sink2"] = {
      {"path/path2",
       "path2_modified"}};  // we will keep old one and not override it in summary.json.

  data_sink_map["sink4"] = {{"path/path4", "path4"}};  // add new sink.
  new_map["test_url1.cmx"] =
      TestDebugDataMapValue{true, std::move(data_sink_map)};  // should still come back as FAIL

  data_sink_map = DataSinkMap();
  data_sink_map["sink1"] = {{"path/path1", "path1"}, {"path/path1_1", "path1_1"}};
  data_sink_map["sink2"] = {{"path/path2_0", "path2_0"}, {"path/path2", "path2"}};
  data_sink_map["sink3"] = {{"path/path3", "path3"}};

  new_map["test_url3.cmx"] = TestDebugDataMapValue{true, std::move(data_sink_map)};
  processor()->WriteSummaryFile(fd, new_map);

  auto expected_map = std::move(map);
  expected_map["test_url1.cmx"].data_sink_map["sink1"]["path/path1_2"] = "path1_2";
  expected_map["test_url1.cmx"].data_sink_map["sink4"] = {{"path/path4", "path4"}};
  expected_map["test_url3.cmx"] = new_map["test_url3.cmx"];

  AssertSummaryJson(expected_map, std::move(fd));
  ASSERT_FALSE(testing::Test::HasFailure());
}

using ProcessDataTest = WriteSummaryFileTest;

namespace {

void AssertStorage(const std::map<std::string, std::map<std::string, std::string>> map,
                   fbl::unique_fd tmp_fd_) {
  std::string result;
  ASSERT_TRUE(files::ReadFileToStringAt(tmp_fd_.get(), kSummaryFile, &result));

  json::JSONParser parser;
  auto doc = parser.ParseFromString(result, "");
  ASSERT_FALSE(parser.HasError()) << parser.error_str();

  ASSERT_TRUE(doc.HasMember("tests"));
  const auto& tests = doc["tests"].GetArray();

  ASSERT_EQ(tests.Size(), map.size());
  for (auto& test : tests) {
    auto test_name = test["name"].GetString();
    auto it = map.find(test_name);
    ASSERT_NE(it, map.end()) << test_name;
    ASSERT_STREQ(test["result"].GetString(), "PASS") << test_name;

    const auto& data_sinks = test["data_sinks"].GetObject();
    auto& data_sink_map = it->second;
    ASSERT_EQ(data_sinks.MemberCount(), data_sink_map.size());
    for (auto& entry : data_sink_map) {
      ASSERT_TRUE(data_sinks.HasMember(entry.first.c_str()));
      const auto& sink = data_sinks[entry.first.c_str()].GetArray();
      ASSERT_EQ(sink.Size(), 1u);
      std::string got_text;
      ASSERT_TRUE(files::ReadFileToStringAt(tmp_fd_.get(), sink[0]["file"].GetString(), &got_text))
          << sink[0]["file"].GetString();
      ASSERT_STREQ(entry.second.c_str(), got_text.c_str()) << entry.first;
    }
  }
}

}  // namespace

TEST_F(ProcessDataTest, ProcessData) {
  TestDebugDataMap map;
  std::map<std::string, std::string> sink_data_map;
  std::map<std::string, std::map<std::string, std::string>> expected_map;
  for (int i = 0; i < 5; i++) {
    zx::vmo vmo;
    ASSERT_EQ(ZX_OK, zx::vmo::create(100, 0, &vmo));
    auto str = fxl::StringPrintf("data for vmo %d", i);
    auto sink_name = fxl::StringPrintf("data_sink%d", i);
    vmo.write(str.c_str(), 0, str.length());
    processor()->ProcessData("test_url1",
                             DataSinkDump{.data_sink = sink_name, .vmo = std::move(vmo)});
    sink_data_map[std::move(sink_name)] = std::move(str);
  }
  expected_map["test_url1"] = std::move(sink_data_map);
  RunLoopUntilIdle();

  AssertStorage(expected_map, GetTempDirFd());
  ASSERT_FALSE(testing::Test::HasFailure());

  sink_data_map = std::map<std::string, std::string>();
  for (int i = 0; i < 5; i++) {
    zx::vmo vmo;
    ASSERT_EQ(ZX_OK, zx::vmo::create(100, 0, &vmo));
    auto str = fxl::StringPrintf("data for vmo for second test %d", i);
    auto sink_name = fxl::StringPrintf("data_sink%d", i);
    vmo.write(str.c_str(), 0, str.length());
    processor()->ProcessData("test_url2",
                             DataSinkDump{.data_sink = sink_name, .vmo = std::move(vmo)});
    sink_data_map[std::move(sink_name)] = std::move(str);
  }

  expected_map["test_url2"] = std::move(sink_data_map);
  RunLoopUntilIdle();

  // Idle signal should be asserted after loop is idle.
  zx_signals_t asserted_signals;
  processor()->GetIdleEvent()->wait_one(IDLE_SIGNAL, zx::time(0), &asserted_signals);
  ASSERT_EQ(asserted_signals & IDLE_SIGNAL, IDLE_SIGNAL);

  AssertStorage(expected_map, GetTempDirFd());
  ASSERT_FALSE(testing::Test::HasFailure());
}
