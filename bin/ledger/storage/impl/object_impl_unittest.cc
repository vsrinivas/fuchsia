// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/object_impl.h"

#include "gtest/gtest.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "peridot/bin/ledger/glue/crypto/rand.h"
#include "peridot/bin/ledger/storage/impl/object_digest.h"
#include "third_party/leveldb/include/leveldb/db.h"

namespace storage {
namespace {

std::string RandomString(size_t size) {
  std::string result;
  result.resize(size);
  glue::RandBytes(&result[0], size);
  return result;
}

::testing::AssertionResult CheckObjectValue(const Object& object,
                                            fxl::StringView digest,
                                            fxl::StringView data) {
  if (object.GetDigest() != digest) {
    return ::testing::AssertionFailure()
           << "Expected id: " << convert::ToHex(digest)
           << ", but got: " << convert::ToHex(object.GetDigest());
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

  zx::vmo vmo;
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

TEST(ObjectImplTest, InlinedObject) {
  std::string data = RandomString(12);
  std::string digest = ComputeObjectDigest(ObjectType::VALUE, data);

  InlinedObject object(digest);
  EXPECT_TRUE(CheckObjectValue(object, digest, data));
}

TEST(ObjectImplTest, StringObject) {
  std::string data = RandomString(256);
  std::string digest = ComputeObjectDigest(ObjectType::VALUE, data);

  StringObject object(digest, data);
  EXPECT_TRUE(CheckObjectValue(object, digest, data));
}

TEST(ObjectImplTest, LevelDBObject) {
  files::ScopedTempDir temp_dir;

  leveldb::DB* db = nullptr;
  leveldb::Options options;
  options.create_if_missing = true;
  leveldb::Status status =
      leveldb::DB::Open(options, temp_dir.path() + "/db", &db);
  ASSERT_TRUE(status.ok());
  std::unique_ptr<leveldb::DB> db_ptr(db);

  leveldb::WriteOptions write_options_;
  leveldb::ReadOptions read_options_;

  std::string data = RandomString(256);
  std::string digest = ComputeObjectDigest(ObjectType::VALUE, data);

  status = db_ptr->Put(write_options_, "", data);
  ASSERT_TRUE(status.ok());
  std::unique_ptr<leveldb::Iterator> iterator(
      db_ptr->NewIterator(read_options_));
  iterator->Seek("");
  ASSERT_TRUE(iterator->Valid());
  ASSERT_TRUE(iterator->key() == "");

  LevelDBObject object(digest, std::move(iterator));
  EXPECT_TRUE(CheckObjectValue(object, digest, data));
}

TEST(ObjectImplTest, VmoObject) {
  std::string data = RandomString(256);
  std::string digest = ComputeObjectDigest(ObjectType::VALUE, data);

  zx::vmo vmo;
  ASSERT_TRUE(fsl::VmoFromString(data, &vmo));

  VmoObject object(digest, std::move(vmo));
  EXPECT_TRUE(CheckObjectValue(object, digest, data));
}

}  // namespace
}  // namespace storage
