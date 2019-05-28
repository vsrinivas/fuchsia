// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/object_impl.h"

#include <lib/fsl/vmo/strings.h>
#include <zircon/syscalls.h>

#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"
#include "src/ledger/bin/storage/impl/btree/encoding.h"
#include "src/ledger/bin/storage/impl/constants.h"
#include "src/ledger/bin/storage/impl/file_index.h"
#include "src/ledger/bin/storage/impl/object_digest.h"
#include "src/ledger/bin/storage/impl/storage_test_utils.h"
#include "src/ledger/bin/storage/public/data_source.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/lib/fxl/logging.h"
#include "third_party/leveldb/include/leveldb/db.h"
#include "util/env_fuchsia.h"

namespace storage {
namespace {
using ::testing::IsEmpty;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;

ObjectIdentifier CreateObjectIdentifier(ObjectDigest digest) {
  return {1u, 2u, std::move(digest)};
}

::testing::AssertionResult CheckObjectValue(const Object& object,
                                            ObjectIdentifier identifier,
                                            fxl::StringView data) {
  if (object.GetIdentifier() != identifier) {
    return ::testing::AssertionFailure()
           << "Expected id: " << identifier
           << ", but got: " << object.GetIdentifier();
  }

  fxl::StringView found_data;
  Status status = object.GetData(&found_data);
  if (status != Status::OK) {
    return ::testing::AssertionFailure()
           << "Unable to call GetData on object, status: " << status;
  }

  if (data != found_data) {
    return ::testing::AssertionFailure()
           << "Expected data: " << convert::ToHex(data)
           << ", but got: " << convert::ToHex(found_data);
  }

  fsl::SizedVmo vmo;
  status = object.GetVmo(&vmo);
  if (status != Status::OK) {
    return ::testing::AssertionFailure()
           << "Unable to call GetVmo on object, status: " << status;
  }

  std::string found_data_in_vmo;
  if (!fsl::StringFromVmo(vmo, &found_data_in_vmo)) {
    return ::testing::AssertionFailure() << "Unable to read from VMO.";
  }

  if (data != found_data_in_vmo) {
    return ::testing::AssertionFailure()
           << "Expected data in vmo: " << convert::ToHex(data)
           << ", but got: " << convert::ToHex(found_data_in_vmo);
  }

  return ::testing::AssertionSuccess();
}

::testing::AssertionResult CheckPieceValue(const Piece& piece,
                                           ObjectIdentifier identifier,
                                           fxl::StringView data) {
  if (piece.GetIdentifier() != identifier) {
    return ::testing::AssertionFailure()
           << "Expected id: " << identifier
           << ", but got: " << piece.GetIdentifier();
  }

  fxl::StringView found_data = piece.GetData();

  if (data != found_data) {
    return ::testing::AssertionFailure()
           << "Expected data: " << convert::ToHex(data)
           << ", but got: " << convert::ToHex(found_data);
  }

  return ::testing::AssertionSuccess();
}

using ObjectImplTest = ledger::TestWithEnvironment;

TEST_F(ObjectImplTest, InlinedPiece) {
  std::string data = RandomString(environment_.random(), 12);
  ObjectIdentifier identifier = CreateObjectIdentifier(
      ComputeObjectDigest(PieceType::CHUNK, ObjectType::BLOB, data));

  const InlinePiece piece(identifier);
  EXPECT_TRUE(CheckPieceValue(piece, identifier, data));
}

TEST_F(ObjectImplTest, DataChunkPiece) {
  std::string data = RandomString(environment_.random(), 12);
  ObjectIdentifier identifier = CreateObjectIdentifier(
      ComputeObjectDigest(PieceType::CHUNK, ObjectType::BLOB, data));

  const DataChunkPiece piece(identifier, DataSource::DataChunk::Create(data));
  EXPECT_TRUE(CheckPieceValue(piece, identifier, data));
}

TEST_F(ObjectImplTest, LevelDBPiece) {
  scoped_tmpfs::ScopedTmpFS tmpfs;
  auto env = leveldb::MakeFuchsiaEnv(tmpfs.root_fd());

  leveldb::DB* db = nullptr;
  leveldb::Options options;
  options.env = env.get();
  options.create_if_missing = true;
  leveldb::Status status = leveldb::DB::Open(options, "db", &db);
  ASSERT_TRUE(status.ok());
  std::unique_ptr<leveldb::DB> db_ptr(db);

  leveldb::WriteOptions write_options_;
  leveldb::ReadOptions read_options_;

  std::string data = RandomString(environment_.random(), 256);
  ObjectIdentifier identifier = CreateObjectIdentifier(
      ComputeObjectDigest(PieceType::CHUNK, ObjectType::BLOB, data));

  status = db_ptr->Put(write_options_, "", data);
  ASSERT_TRUE(status.ok());
  std::unique_ptr<leveldb::Iterator> iterator(
      db_ptr->NewIterator(read_options_));
  iterator->Seek("");
  ASSERT_TRUE(iterator->Valid());
  ASSERT_TRUE(iterator->key() == "");

  const LevelDBPiece piece(identifier, std::move(iterator));
  EXPECT_TRUE(CheckPieceValue(piece, identifier, data));
}

TEST_F(ObjectImplTest, PieceReferences) {
  // Create various types of identifiers for the piece children. Small pieces
  // fit in chunks, while bigger ones are split and yield identifiers of index
  // pieces.
  constexpr size_t kInlineSize = kStorageHashSize;
  const ObjectIdentifier inline_chunk = CreateObjectIdentifier(
      ComputeObjectDigest(PieceType::CHUNK, ObjectType::BLOB,
                          RandomString(environment_.random(), kInlineSize)));
  ASSERT_TRUE(GetObjectDigestInfo(inline_chunk.object_digest()).is_chunk());
  ASSERT_TRUE(GetObjectDigestInfo(inline_chunk.object_digest()).is_inlined());

  const ObjectIdentifier inline_index = CreateObjectIdentifier(
      ComputeObjectDigest(PieceType::INDEX, ObjectType::BLOB,
                          RandomString(environment_.random(), kInlineSize)));
  ASSERT_FALSE(GetObjectDigestInfo(inline_index.object_digest()).is_chunk());
  ASSERT_TRUE(GetObjectDigestInfo(inline_index.object_digest()).is_inlined());

  constexpr size_t kNoInlineSize = kStorageHashSize + 1;
  const ObjectIdentifier noinline_chunk = CreateObjectIdentifier(
      ComputeObjectDigest(PieceType::CHUNK, ObjectType::BLOB,
                          RandomString(environment_.random(), kNoInlineSize)));
  ASSERT_TRUE(GetObjectDigestInfo(noinline_chunk.object_digest()).is_chunk());
  ASSERT_FALSE(
      GetObjectDigestInfo(noinline_chunk.object_digest()).is_inlined());

  const ObjectIdentifier noinline_index = CreateObjectIdentifier(
      ComputeObjectDigest(PieceType::INDEX, ObjectType::BLOB,
                          RandomString(environment_.random(), kNoInlineSize)));
  ASSERT_FALSE(GetObjectDigestInfo(noinline_index.object_digest()).is_chunk());
  ASSERT_FALSE(
      GetObjectDigestInfo(noinline_index.object_digest()).is_inlined());

  // Create the parent piece.
  std::unique_ptr<DataSource::DataChunk> data;
  size_t total_size;
  FileIndexSerialization::BuildFileIndex({{inline_chunk, kInlineSize},
                                          {noinline_chunk, kInlineSize},
                                          {inline_index, kNoInlineSize},
                                          {noinline_index, kNoInlineSize}},
                                         &data, &total_size);
  const ObjectIdentifier identifier = CreateObjectIdentifier(
      ComputeObjectDigest(PieceType::INDEX, ObjectType::BLOB, data->Get()));
  DataChunkPiece piece(identifier, std::move(data));

  // Check that inline children are not included in the references.
  ObjectReferencesAndPriority references;
  ASSERT_EQ(Status::OK, piece.AppendReferences(&references));
  EXPECT_THAT(references,
              UnorderedElementsAre(
                  Pair(noinline_chunk.object_digest(), KeyPriority::EAGER),
                  Pair(noinline_index.object_digest(), KeyPriority::EAGER)));
}

TEST_F(ObjectImplTest, ChunkObject) {
  std::string data = RandomString(environment_.random(), 12);
  ObjectIdentifier identifier = CreateObjectIdentifier(
      ComputeObjectDigest(PieceType::CHUNK, ObjectType::BLOB, data));

  ChunkObject object(std::make_unique<InlinePiece>(identifier));
  EXPECT_TRUE(CheckObjectValue(object, identifier, data));
  auto piece = object.ReleasePiece();
  EXPECT_TRUE(CheckPieceValue(*piece, identifier, data));
}

TEST_F(ObjectImplTest, VmoObject) {
  std::string data = RandomString(environment_.random(), 256);
  ObjectIdentifier identifier = CreateObjectIdentifier(
      ComputeObjectDigest(PieceType::CHUNK, ObjectType::BLOB, data));

  fsl::SizedVmo vmo;
  ASSERT_TRUE(fsl::VmoFromString(data, &vmo));

  VmoObject object(identifier, std::move(vmo));
  EXPECT_TRUE(CheckObjectValue(object, identifier, data));
}

TEST_F(ObjectImplTest, ObjectReferences) {
  // Create various types of identifiers for the object children and values.
  constexpr size_t kInlineSize = kStorageHashSize;
  const ObjectIdentifier inline_blob = CreateObjectIdentifier(
      ComputeObjectDigest(PieceType::CHUNK, ObjectType::BLOB,
                          RandomString(environment_.random(), kInlineSize)));
  ASSERT_TRUE(GetObjectDigestInfo(inline_blob.object_digest()).is_inlined());

  const ObjectIdentifier inline_treenode = CreateObjectIdentifier(
      ComputeObjectDigest(PieceType::CHUNK, ObjectType::TREE_NODE,
                          RandomString(environment_.random(), kInlineSize)));
  ASSERT_TRUE(
      GetObjectDigestInfo(inline_treenode.object_digest()).is_inlined());

  constexpr size_t kNoInlineSize = kStorageHashSize + 1;
  const ObjectIdentifier noinline_blob = CreateObjectIdentifier(
      ComputeObjectDigest(PieceType::CHUNK, ObjectType::BLOB,
                          RandomString(environment_.random(), kNoInlineSize)));
  ASSERT_FALSE(GetObjectDigestInfo(noinline_blob.object_digest()).is_inlined());

  const ObjectIdentifier noinline_treenode = CreateObjectIdentifier(
      ComputeObjectDigest(PieceType::CHUNK, ObjectType::TREE_NODE,
                          RandomString(environment_.random(), kNoInlineSize)));
  ASSERT_FALSE(
      GetObjectDigestInfo(noinline_treenode.object_digest()).is_inlined());

  // Create a TreeNode object.
  const std::string data =
      btree::EncodeNode(/*level=*/0,
                        {
                            Entry{"key01", inline_blob, KeyPriority::EAGER},
                            Entry{"key02", noinline_blob, KeyPriority::EAGER},
                            Entry{"key03", inline_blob, KeyPriority::LAZY},
                            Entry{"key04", noinline_blob, KeyPriority::LAZY},
                        },
                        {
                            {0, inline_treenode},
                            {1, noinline_treenode},
                        });
  const ChunkObject tree_object(std::make_unique<DataChunkPiece>(
      CreateObjectIdentifier(
          ComputeObjectDigest(PieceType::CHUNK, ObjectType::TREE_NODE, data)),
      DataSource::DataChunk::Create(data)));

  // Check that inline children are not included in the references.
  ObjectReferencesAndPriority references;
  ASSERT_EQ(Status::OK, tree_object.AppendReferences(&references));
  EXPECT_THAT(references,
              UnorderedElementsAre(
                  Pair(noinline_blob.object_digest(), KeyPriority::EAGER),
                  Pair(noinline_blob.object_digest(), KeyPriority::LAZY),
                  Pair(noinline_treenode.object_digest(), KeyPriority::EAGER)));

  // Create a blob object with the exact same data and check that it does not
  // return any reference.
  const ChunkObject blob_object(std::make_unique<DataChunkPiece>(
      CreateObjectIdentifier(
          ComputeObjectDigest(PieceType::CHUNK, ObjectType::BLOB, data)),
      DataSource::DataChunk::Create(data)));
  references.clear();
  ASSERT_EQ(Status::OK, blob_object.AppendReferences(&references));
  EXPECT_THAT(references, IsEmpty());

  // Create an invalid tree node object and check that we return an error.
  const ChunkObject invalid_object(std::make_unique<DataChunkPiece>(
      CreateObjectIdentifier(
          ComputeObjectDigest(PieceType::CHUNK, ObjectType::TREE_NODE, "")),
      DataSource::DataChunk::Create("")));
  ASSERT_EQ(Status::DATA_INTEGRITY_ERROR,
            invalid_object.AppendReferences(&references));
}

}  // namespace
}  // namespace storage
