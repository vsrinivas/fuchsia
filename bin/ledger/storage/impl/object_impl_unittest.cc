// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/object_impl.h"

#include <lib/fsl/vmo/strings.h>
#include <zircon/syscalls.h>

#include "gtest/gtest.h"
#include "peridot/bin/ledger/storage/impl/object_digest.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"
#include "third_party/leveldb/include/leveldb/db.h"
#include "util/env_fuchsia.h"

namespace storage {
namespace {

std::string RandomString(size_t size) {
  std::string result;
  result.resize(size);
  zx_cprng_draw(&result[0], size);
  return result;
}

ObjectIdentifier CreateObjectIdentifier(std::string digest) {
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

TEST(ObjectImplTest, InlinedObject) {
  std::string data = RandomString(12);
  ObjectIdentifier identifier =
      CreateObjectIdentifier(ComputeObjectDigest(ObjectType::VALUE, data));

  InlinedObject object(identifier);
  EXPECT_TRUE(CheckObjectValue(object, identifier, data));
}

TEST(ObjectImplTest, StringObject) {
  std::string data = RandomString(256);
  ObjectIdentifier identifier =
      CreateObjectIdentifier(ComputeObjectDigest(ObjectType::VALUE, data));

  StringObject object(identifier, data);
  EXPECT_TRUE(CheckObjectValue(object, identifier, data));
}

TEST(ObjectImplTest, LevelDBObject) {
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

  std::string data = RandomString(256);
  ObjectIdentifier identifier =
      CreateObjectIdentifier(ComputeObjectDigest(ObjectType::VALUE, data));

  status = db_ptr->Put(write_options_, "", data);
  ASSERT_TRUE(status.ok());
  std::unique_ptr<leveldb::Iterator> iterator(
      db_ptr->NewIterator(read_options_));
  iterator->Seek("");
  ASSERT_TRUE(iterator->Valid());
  ASSERT_TRUE(iterator->key() == "");

  LevelDBObject object(identifier, std::move(iterator));
  EXPECT_TRUE(CheckObjectValue(object, identifier, data));
}

TEST(ObjectImplTest, VmoObject) {
  std::string data = RandomString(256);
  ObjectIdentifier identifier =
      CreateObjectIdentifier(ComputeObjectDigest(ObjectType::VALUE, data));

  fsl::SizedVmo vmo;
  ASSERT_TRUE(fsl::VmoFromString(data, &vmo));

  VmoObject object(identifier, std::move(vmo));
  EXPECT_TRUE(CheckObjectValue(object, identifier, data));
}

}  // namespace
}  // namespace storage
