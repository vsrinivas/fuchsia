// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/spawn.h>

#include <algorithm>

#include <rapidjson/document.h>
#include <rapidjson/pointer.h>
#include <src/lib/fsl/vmo/strings.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
namespace {

class LogBinaryTest : public gtest::TestLoopFixture {
 protected:
  LogBinaryTest() = default;

  static void RunBinary(const char** args) {
    ASSERT_EQ(ZX_OK,
              fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, "/pkg/bin/log", args, nullptr));
  }
};

struct TagMessagePair {
  std::string tag;
  std::string message;

  bool operator==(const TagMessagePair& other) const {
    return tag == other.tag && message == other.message;
  }
};

std::vector<TagMessagePair> GetNextMessagePairs(
    fuchsia::diagnostics::BatchIteratorSyncPtr* iterator) {
  fuchsia::diagnostics::BatchIterator_GetNext_Result result;
  if ((*iterator)->GetNext(&result) != ZX_OK || !result.is_response()) {
    std::cout << "Failed to get next batch" << std::endl;
    return {};
  }

  std::vector<TagMessagePair> ret;
  for (const auto& entry : result.response().batch) {
    std::string content;
    if (!fsl::StringFromVmo(entry.json(), &content)) {
      std::cout << "Failed to load JSON from a VMO" << std::endl;
      return {};
    }
    std::cerr << "Received a log entry:\n" << content << std::endl;

    rapidjson::Document document;
    document.Parse(content);
    for (const auto& value : document.GetArray()) {
      auto* tag = rapidjson::Pointer("/metadata/tags/0").Get(value)->GetString();
      auto* message = rapidjson::Pointer("/payload/root/message/value").Get(value)->GetString();
      if (tag == nullptr) {
        std::cout << "Missing tag" << std::endl;
        return {};
      }
      if (message == nullptr) {
        std::cout << "Missing message" << std::endl;
        return {};
      }

      ret.emplace_back(TagMessagePair{.tag = tag, .message = message});
    }
  }

  return ret;
}

// Log some values using the log binary, then check that those values can be read from the
// Archivist.
TEST_F(LogBinaryTest, LogValues) {
  const char* args[] = {"log", "test", "hello", nullptr};
  RunBinary(args);
  const char* args2[] = {"log", "another_test", "hello again", nullptr};
  RunBinary(args2);

  std::string path = std::string("/svc/") + fuchsia::diagnostics::ArchiveAccessor::Name_;

  fuchsia::diagnostics::ArchiveAccessorSyncPtr accessor;
  ASSERT_EQ(ZX_OK,
            fdio_service_connect(path.c_str(), accessor.NewRequest().TakeChannel().release()));

  fuchsia::diagnostics::ClientSelectorConfiguration client_selector_config;
  client_selector_config.set_select_all(true);
  fuchsia::diagnostics::StreamParameters params;
  params.set_data_type(fuchsia::diagnostics::DataType::LOGS);
  params.set_stream_mode(fuchsia::diagnostics::StreamMode::SNAPSHOT_THEN_SUBSCRIBE);
  params.set_format(fuchsia::diagnostics::Format::JSON);
  params.set_client_selector_configuration(std::move(client_selector_config));

  fuchsia::diagnostics::BatchIteratorSyncPtr iterator;
  ASSERT_EQ(ZX_OK, accessor->StreamDiagnostics(std::move(params), iterator.NewRequest()));

  std::vector<TagMessagePair> expected = {
      TagMessagePair{.tag = "test", .message = "hello"},
      TagMessagePair{.tag = "another_test", .message = "hello again"},
  };

  while (!expected.empty()) {
    auto next = GetNextMessagePairs(&iterator);

    ASSERT_FALSE(next.empty())
        << "Ran out of results from the iterator before all expected entries were found.";

    while (!next.empty() && !expected.empty()) {
      auto it = std::find(expected.begin(), expected.end(), next[0]);
      if (it != expected.end()) {
        expected.erase(it);
      }
      next.erase(next.begin());
    }
  }
}

}  // namespace
