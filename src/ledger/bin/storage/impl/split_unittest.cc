// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/split.h"

#include <lib/fit/function.h>
#include <string.h>

#include <map>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/encryption/fake/fake_encryption_service.h"
#include "src/ledger/bin/encryption/primitives/hash.h"
#include "src/ledger/bin/storage/impl/constants.h"
#include "src/ledger/bin/storage/impl/file_index.h"
#include "src/ledger/bin/storage/impl/file_index_generated.h"
#include "src/ledger/bin/storage/impl/object_digest.h"
#include "src/ledger/bin/storage/impl/object_identifier_factory_impl.h"
#include "src/ledger/bin/storage/impl/storage_test_utils.h"
#include "src/ledger/bin/storage/public/data_source.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/ledger/lib/logging/logging.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {
namespace {

using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::Key;
using ::testing::Not;
using ::testing::Pair;
using ::testing::UnorderedElementsAreArray;

constexpr size_t kMinChunkSize = 4 * 1024;
constexpr size_t kMaxChunkSize = std::numeric_limits<uint16_t>::max();

// DataSource that produces 0.
class PathologicalDataSource : public DataSource {
 public:
  explicit PathologicalDataSource(size_t size) : size_(size) {}

  uint64_t GetSize() override { return size_; }
  void Get(fit::function<void(std::unique_ptr<DataChunk>, Status)> callback) override {
    size_t remaining = size_;
    while (remaining) {
      size_t to_send = std::min<size_t>(remaining, 1024u);
      remaining -= to_send;
      callback(DataChunk::Create(std::string(to_send, '\0')), Status::TO_BE_CONTINUED);
    }
    callback(nullptr, Status::DONE);
  }

 private:
  size_t size_;
};

// DataSource that returns an error.
class ErrorDataSource : public DataSource {
 public:
  ErrorDataSource() = default;

  uint64_t GetSize() override { return 1; }
  void Get(fit::function<void(std::unique_ptr<DataChunk>, Status)> callback) override {
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

bool operator==(const Call& lhs, const Call& rhs) {
  return (lhs.digest == rhs.digest) && (lhs.status == rhs.status);
}

struct SplitResult {
  std::vector<Call> calls;
  std::map<ObjectDigest, std::unique_ptr<Piece>> pieces;
};

void DoSplit(DataSource* source, ObjectIdentifierFactory* factory, ObjectType object_type,
             fit::function<void(SplitResult)> callback,
             fit::function<uint64_t(uint64_t)> chunk_permutation = nullptr) {
  auto result = std::make_unique<SplitResult>();
  if (!chunk_permutation) {
    chunk_permutation = [](uint64_t chunk_window_hash) { return chunk_window_hash; };
  }
  SplitDataSource(
      source, object_type,
      [factory](ObjectDigest digest) {
        return encryption::MakeDefaultObjectIdentifier(factory, std::move(digest));
      },
      std::move(chunk_permutation),
      [result = std::move(result), callback = std::move(callback)](
          IterationStatus status, std::unique_ptr<Piece> piece) mutable {
        EXPECT_TRUE(result);
        const auto digest = piece ? piece->GetIdentifier().object_digest() : ObjectDigest();
        if (status != IterationStatus::ERROR) {
          EXPECT_LE(piece->GetData().size(), kMaxChunkSize);
          // Accumulate pieces in result, checking that they match if we have
          // already seen this digest.
          if (result->pieces.count(digest) != 0) {
            EXPECT_EQ(piece->GetData(), result->pieces[digest]->GetData());
          } else {
            result->pieces[digest] = std::move(piece);
          }
        }
        result->calls.push_back({status, digest});
        if (status != IterationStatus::IN_PROGRESS) {
          auto to_send = std::move(*result);
          result.reset();
          callback(std::move(to_send));
        }
      });
}

::testing::AssertionResult ReadFile(const ObjectDigest& digest,
                                    const std::map<ObjectDigest, std::unique_ptr<Piece>>& pieces,
                                    std::string* result, size_t expected_size) {
  size_t start_size = result->size();
  ObjectDigestInfo digest_info = GetObjectDigestInfo(digest);
  if (digest_info.is_inlined()) {
    auto content = ExtractObjectDigestData(digest);
    result->append(content.data(), content.size());
  } else if (digest_info.is_chunk()) {
    if (pieces.count(digest) == 0) {
      return ::testing::AssertionFailure() << "Unknown object.";
    }
    auto content = pieces.at(digest)->GetData();
    result->append(content.data(), content.size());
  } else {
    LEDGER_DCHECK(digest_info.piece_type == PieceType::INDEX);
    if (pieces.count(digest) == 0) {
      return ::testing::AssertionFailure() << "Unknown object.";
    }
    auto content = pieces.at(digest)->GetData();
    const FileIndex* file_index = GetFileIndex(content.data());
    for (const auto* child : *file_index->children()) {
      auto r = ReadFile(ObjectDigest(child->object_identifier()->object_digest()), pieces, result,
                        child->size());
      if (!r) {
        return r;
      }
    }
  }
  if (result->size() - start_size != expected_size) {
    return ::testing::AssertionFailure()
           << "Expected an object of size: " << expected_size
           << " but found an object of size: " << (result->size() - start_size);
  }
  return ::testing::AssertionSuccess();
}

// The first paramater is the size of the value, the second its type.
using SplitSmallValueTest = ::testing::TestWithParam<std::tuple<size_t, ObjectType>>;
using SplitBigValueTest = ::testing::TestWithParam<std::tuple<size_t, ObjectType>>;

TEST_P(SplitSmallValueTest, SmallValue) {
  const std::string content = NewString(std::get<0>(GetParam()));
  const ObjectType object_type = std::get<1>(GetParam());
  auto source = DataSource::Create(content);
  ObjectIdentifierFactoryImpl factory;
  SplitResult split_result;
  DoSplit(source.get(), &factory, object_type,
          [&split_result](SplitResult c) { split_result = std::move(c); });

  ASSERT_EQ(split_result.calls.size(), 1u);
  EXPECT_EQ(split_result.calls[0].status, IterationStatus::DONE);
  ASSERT_EQ(split_result.pieces.size(), 1u);
  EXPECT_EQ(split_result.pieces.begin()->second->GetData(), content);
  EXPECT_EQ(ComputeObjectDigest(PieceType::CHUNK, object_type, content),
            split_result.calls[0].digest);

  std::string found_content;
  ASSERT_TRUE(ReadFile(split_result.calls.back().digest, split_result.pieces, &found_content,
                       content.size()));
  EXPECT_EQ(found_content, content);
}

TEST_P(SplitBigValueTest, BigValues) {
  const std::string content = NewString(std::get<0>(GetParam()));
  const ObjectType object_type = std::get<1>(GetParam());
  auto source = DataSource::Create(content);
  ObjectIdentifierFactoryImpl factory;
  SplitResult split_result;
  DoSplit(source.get(), &factory, object_type,
          [&split_result](SplitResult c) { split_result = std::move(c); });

  EXPECT_EQ(split_result.calls.back().status, IterationStatus::DONE);
  // There is at least 3 calls:
  // 1 index
  // 2 contents (including 1 termination)
  ASSERT_GE(split_result.calls.size(), 3u);

  absl::string_view current = content;
  for (const auto& call : split_result.calls) {
    if (call.status == IterationStatus::IN_PROGRESS &&
        GetObjectDigestInfo(call.digest).is_chunk()) {
      EXPECT_EQ(split_result.pieces[call.digest]->GetData(),
                current.substr(0, split_result.pieces[call.digest]->GetData().size()));
      // Check that object digest is always computed with object_type BLOB for
      // inner pieces (and in particular for chunks here). Only the root must
      // have it set to |object_type|.
      EXPECT_EQ(ComputeObjectDigest(PieceType::CHUNK, ObjectType::BLOB,
                                    split_result.pieces[call.digest]->GetData()),
                call.digest);
      current = current.substr(split_result.pieces[call.digest]->GetData().size());
    }
    if (call.status == IterationStatus::DONE) {
      EXPECT_EQ(PieceType::INDEX, GetObjectDigestInfo(call.digest).piece_type);
      EXPECT_EQ(object_type, GetObjectDigestInfo(call.digest).object_type);
    }
  }

  EXPECT_EQ(current.size(), 0u);

  std::string found_content;
  ASSERT_TRUE(ReadFile(split_result.calls.back().digest, split_result.pieces, &found_content,
                       content.size()));
  EXPECT_EQ(found_content, content);
}

INSTANTIATE_TEST_SUITE_P(
    SplitTest, SplitSmallValueTest,
    ::testing::Combine(::testing::Values(0, 12, kStorageHashSize, kStorageHashSize + 1, 100, 1024,
                                         kMinChunkSize),
                       ::testing::Values(ObjectType::TREE_NODE, ObjectType::BLOB)));

INSTANTIATE_TEST_SUITE_P(
    SplitTest, SplitBigValueTest,
    ::testing::Combine(::testing::Values(kMaxChunkSize + 1, 32 * kMaxChunkSize),
                       ::testing::Values(ObjectType::TREE_NODE, ObjectType::BLOB)));

// A stream of 0s is only cut at the maximal size.
TEST(SplitTest, PathologicalCase) {
  constexpr size_t kDataSize = 1024 * 1024 * 128;
  auto source = std::make_unique<PathologicalDataSource>(kDataSize);
  ObjectIdentifierFactoryImpl factory;
  SplitResult split_result;
  DoSplit(source.get(), &factory, ObjectType::TREE_NODE,
          [&split_result](SplitResult c) { split_result = std::move(c); });

  ASSERT_EQ(split_result.calls.back().status, IterationStatus::DONE);

  size_t total_size = 0u;
  for (const auto& call : split_result.calls) {
    if (call.status == IterationStatus::IN_PROGRESS &&
        GetObjectDigestInfo(call.digest).is_chunk()) {
      total_size += split_result.pieces[call.digest]->GetData().size();
      EXPECT_EQ(split_result.pieces[call.digest]->GetData(),
                std::string(split_result.pieces[call.digest]->GetData().size(), '\0'));
    }
  }
  EXPECT_EQ(total_size, kDataSize);
}

// A stream of 0s of the maximal size + 1 is yielding an INDEX piece pointing
// to an inline CHUNK.
TEST(SplitTest, IndexToInlinePiece) {
  constexpr size_t kDataSize = kMaxChunkSize + 1;
  auto source = std::make_unique<PathologicalDataSource>(kDataSize);
  ObjectIdentifierFactoryImpl factory;
  SplitResult split_result;
  DoSplit(source.get(), &factory, ObjectType::TREE_NODE,
          [&split_result](SplitResult c) { split_result = std::move(c); });

  ASSERT_EQ(split_result.calls.back().status, IterationStatus::DONE);

  // Two CHUNK pieces, one of kMaxChunkSize, another of size 1 (hence inline),
  // and one INDEX piece to bind them.
  ASSERT_EQ(3u, split_result.calls.size());
  // First chunk.
  EXPECT_TRUE(GetObjectDigestInfo(split_result.calls[0].digest).is_chunk());
  EXPECT_FALSE(GetObjectDigestInfo(split_result.calls[0].digest).is_inlined());
  EXPECT_EQ(kMaxChunkSize, split_result.pieces[split_result.calls[0].digest]->GetData().size());
  // Second chunk.
  EXPECT_TRUE(GetObjectDigestInfo(split_result.calls[1].digest).is_chunk());
  EXPECT_TRUE(GetObjectDigestInfo(split_result.calls[1].digest).is_inlined());
  EXPECT_EQ(1u, split_result.pieces[split_result.calls[1].digest]->GetData().size());
  // Index.
  EXPECT_FALSE(GetObjectDigestInfo(split_result.calls[2].digest).is_chunk());
  EXPECT_EQ(ObjectType::TREE_NODE, GetObjectDigestInfo(split_result.calls[2].digest).object_type);
}

TEST(SplitTest, Error) {
  auto source = std::make_unique<ErrorDataSource>();
  ObjectIdentifierFactoryImpl factory;
  SplitResult split_result;
  DoSplit(source.get(), &factory, ObjectType::TREE_NODE,
          [&split_result](SplitResult c) { split_result = std::move(c); });

  ASSERT_EQ(split_result.calls.size(), 1u);
  ASSERT_EQ(split_result.calls.back().status, IterationStatus::ERROR);
}

ObjectIdentifier MakeIndexId(size_t i, ObjectIdentifierFactory* factory) {
  std::string value;
  value.resize(sizeof(i));
  memcpy(&value[0], &i, sizeof(i));
  return encryption::MakeDefaultObjectIdentifier(
      factory, ComputeObjectDigest(PieceType::INDEX, ObjectType::BLOB, value));
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

  ObjectIdentifierFactoryImpl factory;
  for (size_t i = 0; i < parts.size(); ++i) {
    std::vector<FileIndexSerialization::ObjectIdentifierAndSize> children;
    for (size_t child : parts[i]) {
      children.push_back({MakeIndexId(child, &factory), 1});
    }
    size_t total_size;
    FileIndexSerialization::BuildFileIndex(children, &objects[MakeIndexId(i, &factory)],
                                           &total_size);
  }
  IterationStatus status;
  std::set<ObjectIdentifier> identifiers;
  CollectPieces(
      MakeIndexId(0, &factory),
      [&objects](ObjectIdentifier object_identifier,
                 fit::function<void(Status, absl::string_view)> callback) {
        callback(Status::OK, objects[object_identifier]->Get());
      },
      [&status, &identifiers](IterationStatus received_status, ObjectIdentifier identifier) {
        status = received_status;
        if (status == IterationStatus::IN_PROGRESS) {
          identifiers.insert(identifier);
        }
        return true;
      });

  ASSERT_EQ(status, IterationStatus::DONE);
  ASSERT_EQ(identifiers.size(), objects.size());
  for (const auto& identifier : identifiers) {
    EXPECT_EQ(objects.count(identifier), 1u) << "Unknown id: " << identifier;
  }
}

// Test behavior of CollectPieces when the data accessor function returns an
// error in the middle of the iteration.
TEST(SplitTest, CollectPiecesError) {
  const size_t nb_successfull_called = 128;
  IterationStatus status;
  size_t called = 0;
  ObjectIdentifierFactoryImpl factory;
  CollectPieces(
      MakeIndexId(0, &factory),
      [&factory, &called](ObjectIdentifier identifier,
                          fit::function<void(Status, absl::string_view)> callback) {
        if (called >= nb_successfull_called) {
          callback(Status::INTERNAL_ERROR, "");
          return;
        }
        ++called;
        std::vector<FileIndexSerialization::ObjectIdentifierAndSize> children;
        children.push_back({MakeIndexId(2 * called, &factory), 1});
        children.push_back({MakeIndexId(2 * called + 1, &factory), 1});
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
  ASSERT_EQ(status, IterationStatus::ERROR);
}

using SplitTestWithEnvironment = ledger::TestWithEnvironment;

// Test that changing the hash permutation function changes the resulting split.
TEST_F(SplitTestWithEnvironment, DifferentPermutations) {
  const std::string content =
      RandomString(environment_.random(), 4ul * std::numeric_limits<uint16_t>::max());
  auto bit_generator = environment_.random()->NewBitGenerator<uint64_t>();

  auto source = DataSource::Create(content);
  ObjectIdentifierFactoryImpl factory;
  SplitResult split_result1;
  uint64_t d1 =
      std::uniform_int_distribution(0ul, std::numeric_limits<uint64_t>::max())(bit_generator);
  DoSplit(
      source.get(), &factory, ObjectType::BLOB,
      [&split_result1](SplitResult c) { split_result1 = std::move(c); },
      [&d1](uint64_t chunk_window_hash) { return chunk_window_hash ^ d1; });
  EXPECT_EQ(split_result1.calls.back().status, IterationStatus::DONE);

  source = DataSource::Create(content);
  SplitResult split_result2;
  uint64_t d2 =
      std::uniform_int_distribution(0ul, std::numeric_limits<uint64_t>::max())(bit_generator);
  DoSplit(
      source.get(), &factory, ObjectType::BLOB,
      [&split_result2](SplitResult c) { split_result2 = std::move(c); },
      [&d2](uint64_t chunk_window_hash) { return chunk_window_hash ^ d2; });
  EXPECT_EQ(split_result2.calls.back().status, IterationStatus::DONE);

  EXPECT_NE(split_result1.calls, split_result2.calls);
}

}  // namespace
}  // namespace storage
