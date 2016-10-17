// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/impl/object_impl.h"

#include <algorithm>
#include <memory>

#include "apps/ledger/glue/crypto/base64.h"
#include "apps/ledger/glue/crypto/rand.h"
#include "gtest/gtest.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/files/scoped_temp_dir.h"
#include "lib/ftl/logging.h"

namespace storage {
namespace {

const size_t kFileSize = 256;

std::string RandomString(size_t size) {
  std::string result;
  result.resize(size);
  glue::RandBytes(&result[0], size);
  return result;
}

std::string ObjectFilePathFor(const std::string& path, ObjectIdView id) {
  std::string base64;
  glue::Base64Encode(id, &base64);
  std::replace(base64.begin(), base64.end(), '/', '-');
  return path + '/' + base64;
}

class ObjectTest : public ::testing::Test {
 public:
  ObjectTest() {}

  ~ObjectTest() override {}

  // Test:
  void SetUp() override {
    std::srand(0);

    object_id_ = RandomString(32);
    object_file_path_ = ObjectFilePathFor(object_dir_.path(), object_id_);
  }

 protected:
  std::string object_file_path_;
  ObjectId object_id_;

 private:
  files::ScopedTempDir object_dir_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ObjectTest);
};

TEST_F(ObjectTest, Object) {
  std::string data = RandomString(kFileSize);
  EXPECT_TRUE(files::WriteFile(object_file_path_, data.data(), kFileSize));

  ObjectImpl object((std::string(object_id_)), std::string(object_file_path_));
  EXPECT_EQ(object_id_, object.GetId());
  ftl::StringView foundData;
  EXPECT_EQ(Status::OK, object.GetData(&foundData));
  EXPECT_EQ(kFileSize, foundData.size());
  EXPECT_EQ(0, memcmp(data.data(), foundData.data(), kFileSize));
}

}  // namespace
}  // namespace storage
