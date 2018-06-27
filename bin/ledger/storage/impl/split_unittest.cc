// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/split.h"

#include <string.h>

#include <lib/fit/function.h>

#include "gtest/gtest.h"
#include "lib/fxl/functional/make_copyable.h"
#include "peridot/bin/ledger/encryption/fake/fake_encryption_service.h"
#include "peridot/bin/ledger/encryption/primitives/hash.h"
#include "peridot/bin/ledger/storage/impl/constants.h"
#include "peridot/bin/ledger/storage/impl/file_index.h"
#include "peridot/bin/ledger/storage/impl/file_index_generated.h"
#include "peridot/bin/ledger/storage/impl/object_digest.h"
#include "peridot/bin/ledger/storage/public/data_source.h"

namespace storage {
namespace {

constexpr size_t kMinChunkSize = 4 * 1024;
constexpr size_t kMaxChunkSize = std::numeric_limits<uint16_t>::max();

// DataSource that produces 0.
class PathologicalDataSource : public DataSource {
 public:
  explicit PathologicalDataSource(size_t size) : size_(size) {}

  uint64_t GetSize() override { return size_; }
  void Get(fit::function<void(std::unique_ptr<DataChunk>, Status)> callback)
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
  void Get(fit::function<void(std::unique_ptr<DataChunk>, Status)> callback)
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
  IterationStatus status;
  ObjectDigest digest;
};

struct SplitResult {
  std::vector<Call> calls;
  std::map<ObjectDigest, std::unique_ptr<DataSource::DataChunk>> data;
};

void DoSplit(DataSource* source, fit::function<void(SplitResult)> callback) {
  auto result = std::make_unique<SplitResult>();
  SplitDataSource(
      source,
      fxl::MakeCopyable(
          [result = std::move(result), callback = std::move(callback)](
              IterationStatus status, ObjectDigest digest,
              std::unique_ptr<DataSource::DataChunk> data) mutable {
            EXPECT_TRUE(result);
            if (status == IterationStatus::IN_PROGRESS) {
              EXPECT_LE(data->Get().size(), kMaxChunkSize);
              if (result->data.count(digest) != 0) {
                EXPECT_EQ(result->data[digest]->Get(), data->Get());
              } else {
                result->data[digest] = std::move(data);
              }
            }
            result->calls.push_back({status, digest});
            if (status != IterationStatus::IN_PROGRESS) {
              auto to_send = std::move(*result);
              result.reset();
              callback(std::move(to_send));
            }
            return encryption::MakeDefaultObjectIdentifier(std::move(digest));
          }));
}

::testing::AssertionResult ReadFile(
    const ObjectDigest& digest,
    const std::map<ObjectDigest, std::unique_ptr<DataSource::DataChunk>>& data,
    std::string* result, size_t expected_size) {
  size_t start_size = result->size();
  switch (GetObjectDigestType(digest)) {
    case ObjectDigestType::INLINE: {
      auto content = ExtractObjectDigestData(digest);
      result->append(content.data(), content.size());
      break;
    }
    case ObjectDigestType::VALUE_HASH: {
      if (data.count(digest) == 0) {
        return ::testing::AssertionFailure() << "Unknown object.";
      }
      auto content = data.at(digest)->Get();
      result->append(content.data(), content.size());
      break;
    }
    case ObjectDigestType::INDEX_HASH: {
      if (data.count(digest) == 0) {
        return ::testing::AssertionFailure() << "Unknown object.";
      }
      auto content = data.at(digest)->Get();
      const FileIndex* file_index = GetFileIndex(content.data());
      for (const auto* child : *file_index->children()) {
        auto r = ReadFile(
            convert::ToString(child->object_identifier()->object_digest()),
            data, result, child->size());
        if (!r) {
          return r;
        }
      }
      break;
    }
  }
  if (result->size() - start_size != expected_size) {
    return ::testing::AssertionFailure()
           << "Expected an object of size: " << expected_size
           << " but found an object of size: " << (result->size() - start_size);
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
  EXPECT_EQ(IterationStatus::DONE, split_result.calls[1].status);
  ASSERT_EQ(1u, split_result.data.size());
  EXPECT_EQ(content, split_result.data.begin()->second->Get());
  EXPECT_EQ(split_result.calls[1].digest,
            ComputeObjectDigest(ObjectType::VALUE, content));

  std::string found_content;
  ASSERT_TRUE(ReadFile(split_result.calls.back().digest, split_result.data,
                       &found_content, content.size()));
  EXPECT_EQ(content, found_content);
}

TEST_P(SplitBigValueTest, BigValues) {
  std::string content = NewString(GetParam());
  auto source = DataSource::Create(content);
  SplitResult split_result;
  DoSplit(source.get(),
          [&split_result](SplitResult c) { split_result = std::move(c); });

  EXPECT_EQ(IterationStatus::DONE, split_result.calls.back().status);
  // There is at least 4 calls:
  // 1 index
  // 2 contents
  // 1 termination
  ASSERT_GE(split_result.calls.size(), 4u);

  fxl::StringView current = content;
  for (const auto& call : split_result.calls) {
    if (call.status == IterationStatus::IN_PROGRESS &&
        GetObjectDigestType(call.digest) == ObjectDigestType::VALUE_HASH) {
      EXPECT_EQ(current.substr(0, split_result.data[call.digest]->Get().size()),
                split_result.data[call.digest]->Get());
      current = current.substr(split_result.data[call.digest]->Get().size());
    }
  }

  EXPECT_EQ(0u, current.size());

  std::string found_content;
  ASSERT_TRUE(ReadFile(split_result.calls.back().digest, split_result.data,
                       &found_content, content.size()));
  EXPECT_EQ(content, found_content);
}

INSTANTIATE_TEST_CASE_P(SplitTest, SplitSmallValueTest,
                        ::testing::Values(0, 12, kStorageHashSize,
                                          kStorageHashSize + 1, 100, 1024,
                                          kMinChunkSize));

INSTANTIATE_TEST_CASE_P(SplitTest, SplitBigValueTest,
                        ::testing::Values(kMaxChunkSize + 1,
                                          32 * kMaxChunkSize));

// A stream of 0s is only cut at the maximal size.
TEST(SplitTest, PathologicalCase) {
  constexpr size_t kDataSize = 1024 * 1024 * 128;
  auto source = std::make_unique<PathologicalDataSource>(kDataSize);
  SplitResult split_result;
  DoSplit(source.get(),
          [&split_result](SplitResult c) { split_result = std::move(c); });

  ASSERT_EQ(IterationStatus::DONE, split_result.calls.back().status);

  size_t total_size = 0u;
  for (const auto& call : split_result.calls) {
    if (call.status == IterationStatus::IN_PROGRESS &&
        GetObjectDigestType(call.digest) == ObjectDigestType::VALUE_HASH) {
      total_size += split_result.data[call.digest]->Get().size();
      EXPECT_EQ(std::string(split_result.data[call.digest]->Get().size(), '\0'),
                split_result.data[call.digest]->Get());
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
  ASSERT_EQ(IterationStatus::ERROR, split_result.calls.back().status);
}

ObjectIdentifier MakeIndexId(size_t i) {
  std::string value;
  value.resize(sizeof(i));
  memcpy(&value[0], &i, sizeof(i));
  return encryption::MakeDefaultObjectIdentifier(
      ComputeObjectDigest(ObjectType::INDEX, value));
}

TEST(SplitTest, CollectPieces) {
  // Define indexed files. Each index represents an index file. The content is
  // itself a of index in |parts| that represent the children of the entry.
  std::vector<std::vector<size_t>> parts = {
      // clang-format off
      {1, 2, 3},
      {4, 5},
      {4, 6, 7},
      {7, 8, 9},
      {10, 11},
      {},
      {},
      {},
      {},
      {},
      {},
      {}
      // clang-format on
  };

  for (const auto& children : parts) {
    for (size_t child : children) {
      EXPECT_LT(child, parts.size());
    }
  }

  std::map<ObjectIdentifier, std::unique_ptr<DataSource::DataChunk>> objects;

  for (size_t i = 0; i < parts.size(); ++i) {
    std::vector<FileIndexSerialization::ObjectIdentifierAndSize> children;
    for (size_t child : parts[i]) {
      children.push_back({MakeIndexId(child), 1});
    }
    size_t total_size;
    FileIndexSerialization::BuildFileIndex(children, &objects[MakeIndexId(i)],
                                           &total_size);
  }
  IterationStatus status;
  std::set<ObjectIdentifier> identifiers;
  CollectPieces(
      MakeIndexId(0),
      [&objects](ObjectIdentifier object_identifier,
                 fit::function<void(Status, fxl::StringView)> callback) {
        callback(Status::OK, objects[object_identifier]->Get());
      },
      [&status, &identifiers](IterationStatus received_status,
                              ObjectIdentifier identifier) {
        status = received_status;
        if (status == IterationStatus::IN_PROGRESS) {
          identifiers.insert(identifier);
        }
        return true;
      });

  ASSERT_EQ(IterationStatus::DONE, status);
  ASSERT_EQ(objects.size(), identifiers.size());
  for (const auto& identifier : identifiers) {
    EXPECT_EQ(1u, objects.count(identifier)) << "Unknown id: " << identifier;
  }
}

// Test behavior of CollectPieces when the data accessor function returns an
// error in the middle of the iteration.
TEST(SplitTest, CollectPiecesError) {
  const size_t nb_successfull_called = 128;
  IterationStatus status;
  size_t called = 0;
  CollectPieces(
      MakeIndexId(0),
      [&called](ObjectIdentifier identifier,
                fit::function<void(Status, fxl::StringView)> callback) {
        if (called >= nb_successfull_called) {
          callback(Status::INTERNAL_IO_ERROR, "");
          return;
        }
        ++called;
        std::vector<FileIndexSerialization::ObjectIdentifierAndSize> children;
        children.push_back({MakeIndexId(2 * called), 1});
        children.push_back({MakeIndexId(2 * called + 1), 1});
        std::unique_ptr<DataSource::DataChunk> data;
        size_t total_size;
        FileIndexSerialization::BuildFileIndex(children, &data, &total_size);
        callback(Status::OK, data->Get());
      },
      [&status](IterationStatus received_status, ObjectIdentifier identifier) {
        status = received_status;
        return true;
      });
  EXPECT_GE(called, nb_successfull_called);
  ASSERT_EQ(IterationStatus::ERROR, status);
}

}  // namespace
}  // namespace storage
