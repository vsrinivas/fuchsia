// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_sync/impl/local_version_checker.h"

#include <unordered_map>

#include "gtest/gtest.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/files/scoped_temp_dir.h"
#include "lib/ftl/logging.h"

namespace cloud_sync {
namespace {
class FakeFirebase : public firebase::Firebase {
 public:
  FakeFirebase() {}
  ~FakeFirebase() override {}

  virtual void Get(const std::string& key,
                   const std::vector<std::string>& query_params,
                   const std::function<void(firebase::Status status,
                                            const rapidjson::Value& value)>&
                       callback) override {
    rapidjson::Document document;
    if (values.count(key)) {
      document.Parse(values[key]);
    } else {
      document.Parse("null");
    }
    callback(returned_status, document);
  }

  virtual void Put(
      const std::string& key,
      const std::vector<std::string>& query_params,
      const std::string& data,
      const std::function<void(firebase::Status status)>& callback) override {
    rapidjson::Document document;
    document.Parse(data.c_str(), data.size());
    FTL_DCHECK(!document.HasParseError());
    values[key] = data;
    callback(returned_status);
  }

  void Patch(
      const std::string& key,
      const std::vector<std::string>& query_params,
      const std::string& data,
      const std::function<void(firebase::Status status)>& callback) override {
    FTL_NOTREACHED();
  }

  void Delete(
      const std::string& key,
      const std::vector<std::string>& query_params,
      const std::function<void(firebase::Status status)>& callback) override {
    FTL_NOTREACHED();
  }

  void Watch(const std::string& key,
             const std::vector<std::string>& query_params,
             firebase::WatchClient* watch_client) override {
    FTL_NOTREACHED();
  }

  void UnWatch(firebase::WatchClient* watch_client) override {
    FTL_NOTREACHED();
  }

  firebase::Status returned_status = firebase::Status::OK;
  std::unordered_map<std::string, std::string> values;
};

class LocalVersionCheckerTest : public ::testing::Test {
 public:
  LocalVersionCheckerTest() {}

 protected:
  void SetUp() override {
    ResetFile();
    ResetFirebase();
  }

  void ResetFile() {
    tmp_dir = std::make_unique<files::ScopedTempDir>();
    local_version_file = tmp_dir->path() + "/version";
  }

  void ResetFirebase() { firebase = std::make_unique<FakeFirebase>(); }

  LocalVersionChecker::Status CheckCloudVersion() {
    LocalVersionChecker checker;

    auto result = LocalVersionChecker::Status::NETWORK_ERROR;
    checker.CheckCloudVersion(
        firebase.get(), local_version_file,
        [&result](auto found_result) { result = found_result; });
    return result;
  }

  std::string GetFileContent() {
    std::string result;
    EXPECT_TRUE(files::ReadFileToString(local_version_file, &result));
    return result;
  }

  std::unique_ptr<files::ScopedTempDir> tmp_dir;
  std::string local_version_file;
  std::unique_ptr<FakeFirebase> firebase;
};

TEST_F(LocalVersionCheckerTest, NoLocalVersionNoRemoteVersion) {
  EXPECT_EQ(LocalVersionChecker::Status::OK, CheckCloudVersion());

  EXPECT_TRUE(files::IsFile(local_version_file));
  ASSERT_EQ(1u, firebase->values.size());
  EXPECT_NE(std::string::npos,
            firebase->values.begin()->first.find(GetFileContent()));
}

TEST_F(LocalVersionCheckerTest, CompatibleLocalAndRemoteVersion) {
  ASSERT_EQ(LocalVersionChecker::Status::OK, CheckCloudVersion());

  EXPECT_EQ(LocalVersionChecker::Status::OK, CheckCloudVersion());

  EXPECT_TRUE(files::IsFile(local_version_file));
  ASSERT_EQ(1u, firebase->values.size());
  EXPECT_NE(std::string::npos,
            firebase->values.begin()->first.find(GetFileContent()));
}

TEST_F(LocalVersionCheckerTest, NoLocalVersionOtherRemoteVersion) {
  ASSERT_EQ(LocalVersionChecker::Status::OK, CheckCloudVersion());

  ResetFile();
  EXPECT_EQ(LocalVersionChecker::Status::OK, CheckCloudVersion());
  EXPECT_TRUE(files::IsFile(local_version_file));
  EXPECT_EQ(2u, firebase->values.size());
}

TEST_F(LocalVersionCheckerTest, IncompatibleVersions) {
  ASSERT_EQ(LocalVersionChecker::Status::OK, CheckCloudVersion());

  ResetFirebase();
  EXPECT_EQ(LocalVersionChecker::Status::INCOMPATIBLE, CheckCloudVersion());
}

TEST_F(LocalVersionCheckerTest, IoErrorOnPut) {
  firebase->returned_status = firebase::Status::NETWORK_ERROR;

  EXPECT_EQ(LocalVersionChecker::Status::NETWORK_ERROR, CheckCloudVersion());
}

TEST_F(LocalVersionCheckerTest, IoErrorOnGet) {
  ASSERT_EQ(LocalVersionChecker::Status::OK, CheckCloudVersion());

  firebase->returned_status = firebase::Status::NETWORK_ERROR;
  EXPECT_EQ(LocalVersionChecker::Status::NETWORK_ERROR, CheckCloudVersion());
}

}  // namespace
}  // namespace cloud_sync
