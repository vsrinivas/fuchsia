// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/split.h"

#include <string.h>

#include "apps/ledger/src/glue/crypto/hash.h"
#include "apps/ledger/src/glue/crypto/rand.h"
#include "apps/ledger/src/storage/impl/constants.h"
#include "apps/ledger/src/storage/impl/file_index_generated.h"
#include "apps/ledger/src/storage/impl/object_id.h"
#include "apps/ledger/src/storage/public/data_source.h"
#include "gtest/gtest.h"
#include "lib/ftl/functional/make_copyable.h"

namespace storage {
namespace {

constexpr size_t kMinChunkSize = 4 * 1024;
constexpr size_t kMaxChunkSize = std::numeric_limits<uint16_t>::max();

// DataSource that produces 0.
class PathologicalDataSource : public DataSource {
 public:
  PathologicalDataSource(size_t size) : size_(size) {}

  uint64_t GetSize() override { return size_; }
  void Get(std::function<void(std::unique_ptr<DataChunk>, Status)> callback)
      override {
    size_t remaining = size_;
    while (remaining) {
      size_t to_send = std::min<size_t>(remaining, 1024u);
      remaining -= to_send;
      callback(DataChunk::Create(std::string(to_send, '\0')),
               Status::TO_BE_CONTINUED);
    }
    callback(nullptr, Status::DONE);
  }

 private:
  size_t size_;
};

// DataSource that returns an error.
class ErrorDataSource : public DataSource {
 public:
  ErrorDataSource() {}

  uint64_t GetSize() override { return 1; }
  void Get(std::function<void(std::unique_ptr<DataChunk>, Status)> callback)
      override {
    callback(nullptr, Status::ERROR);
  }
};

std::string NewString(size_t size) {
  std::string content;
  content.resize(size);
  size_t i;
  for (i = 0; i + sizeof(size_t) < content.size(); i += sizeof(size_t)) {
    memcpy(&content[0] + i, &i, sizeof(i));
  }
  memcpy(&content[0] + i, &i, size % sizeof(size_t));
  return content;
}

struct Call {
  SplitStatus status;
  ObjectId id;
};

struct SplitResult {
  std::vector<Call> calls;
  std::map<ObjectId, std::unique_ptr<DataSource::DataChunk>> data;
};

void DoSplit(DataSource* source, std::function<void(SplitResult)> callback) {
  auto result = std::make_unique<SplitResult>();
  SplitDataSource(source, ftl::MakeCopyable([
                    result = std::move(result), callback = std::move(callback)
                  ](SplitStatus status, ObjectId id,
                          std::unique_ptr<DataSource::DataChunk> data) mutable {
                    ASSERT_TRUE(result);
                    if (status == SplitStatus::IN_PROGRESS) {
                      EXPECT_LE(data->Get().size(), kMaxChunkSize);
                      if (result->data.count(id) != 0) {
                        EXPECT_EQ(result->data[id]->Get(), data->Get());
                      } else {
                        result->data[id] = std::move(data);
                      }
                    }
                    result->calls.push_back({status, std::move(id)});
                    if (status != SplitStatus::IN_PROGRESS) {
                      auto to_send = std::move(*result);
                      result.reset();
                      callback(std::move(to_send));
                    }
                  }));
}

::testing::AssertionResult ReadFile(
    const ObjectId& id,
    const std::map<ObjectId, std::unique_ptr<DataSource::DataChunk>>& data,
    std::string* result) {
  switch (GetObjectIdType(id)) {
    case ObjectIdType::INLINE: {
      auto content = ExtractObjectIdData(id);
      result->append(content.data(), content.size());
      break;
    }
    case ObjectIdType::VALUE_HASH: {
      if (data.count(id) == 0) {
        return ::testing::AssertionFailure() << "Unknown object.";
      }
      auto content = data.at(id)->Get();
      result->append(content.data(), content.size());
      break;
    }
    case ObjectIdType::INDEX_HASH: {
      if (data.count(id) == 0) {
        return ::testing::AssertionFailure() << "Unknown object.";
      }
      auto content = data.at(id)->Get();
      const FileIndex* file_index = GetFileIndex(content.data());
      for (const auto* child : *file_index->children()) {
        auto r = ReadFile(convert::ToString(child->object_id()), data, result);
        if (!r) {
          return r;
        }
      }
      break;
    }
  }
  return ::testing::AssertionSuccess();
}

class SplitSmallValueTest : public ::testing::TestWithParam<size_t> {};

class SplitBigValueTest : public ::testing::TestWithParam<size_t> {};

TEST_P(SplitSmallValueTest, SmallValue) {
  std::string content = NewString(GetParam());
  auto source = DataSource::Create(content);
  SplitResult split_result;
  DoSplit(source.get(),
          [&split_result](SplitResult c) { split_result = std::move(c); });

  ASSERT_EQ(2u, split_result.calls.size());
  EXPECT_EQ(SplitStatus::DONE, split_result.calls[1].status);
  ASSERT_EQ(1u, split_result.data.size());
  EXPECT_EQ(content, split_result.data.begin()->second->Get());
  EXPECT_EQ(split_result.calls[1].id,
            ComputeObjectId(ObjectType::VALUE, content));

  std::string found_content;
  ASSERT_TRUE(ReadFile(split_result.calls.back().id, split_result.data,
                       &found_content));
  EXPECT_EQ(content, found_content);
}

TEST_P(SplitBigValueTest, BigValues) {
  std::string content = NewString(GetParam());
  auto source = DataSource::Create(content);
  SplitResult split_result;
  DoSplit(source.get(),
          [&split_result](SplitResult c) { split_result = std::move(c); });

  EXPECT_EQ(SplitStatus::DONE, split_result.calls.back().status);
  // There is at least 4 calls:
  // 1 index
  // 2 contents
  // 1 termination
  ASSERT_GE(split_result.calls.size(), 4u);

  ftl::StringView current = content;
  for (const auto& call : split_result.calls) {
    if (call.status == SplitStatus::IN_PROGRESS &&
        GetObjectIdType(call.id) == ObjectIdType::VALUE_HASH) {
      EXPECT_EQ(current.substr(0, split_result.data[call.id]->Get().size()),
                split_result.data[call.id]->Get());
      current = current.substr(split_result.data[call.id]->Get().size());
    }
  }

  EXPECT_EQ(0u, current.size());

  std::string found_content;
  ASSERT_TRUE(ReadFile(split_result.calls.back().id, split_result.data,
                       &found_content));
  EXPECT_EQ(content, found_content);
}

INSTANTIATE_TEST_CASE_P(SplitTest,
                        SplitSmallValueTest,
                        ::testing::Values(0,
                                          12,
                                          kObjectHashSize,
                                          kObjectHashSize + 1,
                                          100,
                                          1024,
                                          kMinChunkSize));

INSTANTIATE_TEST_CASE_P(SplitTest,
                        SplitBigValueTest,
                        ::testing::Values(kMaxChunkSize + 1,
                                          32 * kMaxChunkSize));

// A stream of 0s is only cut at the maximal size.
TEST(SplitTest, PathologicalCase) {
  constexpr size_t kDataSize = 1024 * 1024 * 128;
  auto source = std::make_unique<PathologicalDataSource>(kDataSize);
  SplitResult split_result;
  DoSplit(source.get(),
          [&split_result](SplitResult c) { split_result = std::move(c); });

  ASSERT_EQ(SplitStatus::DONE, split_result.calls.back().status);

  size_t total_size = 0u;
  for (const auto& call : split_result.calls) {
    if (call.status == SplitStatus::IN_PROGRESS &&
        GetObjectIdType(call.id) == ObjectIdType::VALUE_HASH) {
      total_size += split_result.data[call.id]->Get().size();
      EXPECT_EQ(std::string(split_result.data[call.id]->Get().size(), '\0'),
                split_result.data[call.id]->Get());
    }
  }

  EXPECT_EQ(kDataSize, total_size);
}

TEST(SplitTest, Error) {
  auto source = std::make_unique<ErrorDataSource>();
  SplitResult split_result;
  DoSplit(source.get(),
          [&split_result](SplitResult c) { split_result = std::move(c); });

  ASSERT_EQ(1u, split_result.calls.size());
  ASSERT_EQ(SplitStatus::ERROR, split_result.calls.back().status);
}

}  // namespace
}  // namespace storage
