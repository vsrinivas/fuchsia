// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firebase/device_set/cloud_device_set_impl.h"

#include <map>

#include "lib/callback/capture.h"
#include "lib/callback/set_when_called.h"
#include "lib/fxl/logging.h"
#include "lib/gtest/test_with_loop.h"

namespace cloud_provider_firebase {
namespace {
class FakeFirebase : public firebase::Firebase {
 public:
  FakeFirebase() {}
  ~FakeFirebase() override {}

  void Get(const std::string& /*key*/,
           const std::vector<std::string>& query_params,
           std::function<void(firebase::Status status,
                              const rapidjson::Value& value)>
               callback) override {
    get_query_params.push_back(query_params);
    rapidjson::Document document;
    document.Parse(returned_value);
    callback(returned_status, document);
  }

  void Put(const std::string& /*key*/,
           const std::vector<std::string>& query_params,
           const std::string& data,
           std::function<void(firebase::Status status)> callback) override {
    put_query_params.push_back(query_params);
    put_data.push_back(data);
    callback(returned_status);
  }

  void Patch(
      const std::string& /*key*/,
      const std::vector<std::string>& /*query_params*/,
      const std::string& /*data*/,
      std::function<void(firebase::Status status)> /*callback*/) override {
    FXL_NOTREACHED();
  }

  void Delete(const std::string& key,
              const std::vector<std::string>& query_params,
              std::function<void(firebase::Status status)> callback) override {
    delete_keys.push_back(key);
    delete_query_params.push_back(query_params);
    callback(returned_status);
  }

  void Watch(const std::string& key,
             const std::vector<std::string>& query_params,
             firebase::WatchClient* watch_client) override {
    watch_query_params.push_back(query_params);
    watch_keys.push_back(key);
    this->watch_client = watch_client;
  }

  void UnWatch(firebase::WatchClient* watch_client) override {
    EXPECT_EQ(this->watch_client, watch_client);
    unwatch_calls++;
  }

  firebase::Status returned_status = firebase::Status::OK;
  std::string returned_value;
  std::map<std::string, std::string> values;
  std::vector<std::vector<std::string>> get_query_params;
  std::vector<std::vector<std::string>> put_query_params;
  std::vector<std::string> put_data;
  std::vector<std::vector<std::string>> delete_query_params;
  std::vector<std::string> delete_keys;
  std::vector<std::string> watch_keys;
  std::vector<std::vector<std::string>> watch_query_params;
  firebase::WatchClient* watch_client;
  int unwatch_calls = 0;
};

class CloudDeviceSetImplTest : public gtest::TestWithLoop {
 public:
  CloudDeviceSetImplTest() : cloud_device_set_(InitFirebase()) {}

 protected:
  FakeFirebase* firebase_;
  CloudDeviceSetImpl cloud_device_set_;

  std::unique_ptr<firebase::Firebase> InitFirebase() {
    auto firebase = std::make_unique<FakeFirebase>();
    firebase_ = firebase.get();
    return firebase;
  }
};

TEST_F(CloudDeviceSetImplTest, CheckFingerprintOk) {
  firebase_->returned_value = "true";
  bool called;
  CloudDeviceSet::Status status;
  cloud_device_set_.CheckFingerprint(
      "some-token", "some-fingerprint",
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(CloudDeviceSet::Status::OK, status);
  EXPECT_EQ((std::vector<std::vector<std::string>>{{"auth=some-token"}}),
            firebase_->get_query_params);
}

TEST_F(CloudDeviceSetImplTest, CheckFingerprintErased) {
  firebase_->returned_value = "null";
  bool called;
  CloudDeviceSet::Status status;
  cloud_device_set_.CheckFingerprint(
      "some-token", "some-fingerprint",
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(CloudDeviceSet::Status::ERASED, status);
  EXPECT_EQ((std::vector<std::vector<std::string>>{{"auth=some-token"}}),
            firebase_->get_query_params);
}

TEST_F(CloudDeviceSetImplTest, CheckFingerprintDeleteInCallback) {
  firebase_->returned_value = "null";
  bool called;
  CloudDeviceSet::Status status;
  auto checker = std::make_unique<CloudDeviceSetImpl>(InitFirebase());
  checker->CheckFingerprint("some-token", "some-fingerprint",
                            [&checker, &status, &called](auto s) {
                              checker.reset();
                              status = s;
                              called = true;
                            });
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_FALSE(checker);
  EXPECT_EQ(CloudDeviceSet::Status::ERASED, status);
}

TEST_F(CloudDeviceSetImplTest, SetFingerprintOk) {
  bool called;
  CloudDeviceSet::Status status;
  cloud_device_set_.SetFingerprint(
      "some-token", "some-fingerprint",
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(CloudDeviceSet::Status::OK, status);
  EXPECT_EQ((std::vector<std::vector<std::string>>{{"auth=some-token"}}),
            firebase_->put_query_params);
  EXPECT_EQ((std::vector<std::string>{"{\".sv\": \"timestamp\"}"}),
            firebase_->put_data);
}

TEST_F(CloudDeviceSetImplTest, WatchFingerprint) {
  bool called = false;
  CloudDeviceSet::Status status;
  cloud_device_set_.WatchFingerprint("some-token", "some-fingerprint",
                                     [&status, &called](auto s) {
                                       status = s;
                                       called = true;
                                     });
  EXPECT_EQ((std::vector<std::vector<std::string>>{{"auth=some-token"}}),
            firebase_->watch_query_params);
  EXPECT_EQ((std::vector<std::string>{"__metadata/devices/some-fingerprint"}),
            firebase_->watch_keys);
  ASSERT_TRUE(firebase_->watch_client);

  {
    rapidjson::Document document;
    document.Parse("true");
    firebase_->watch_client->OnPut("/", document);
  }
  EXPECT_TRUE(called);
  EXPECT_EQ(CloudDeviceSet::Status::OK, status);

  called = false;
  {
    rapidjson::Document document;
    document.Parse("null");
    firebase_->watch_client->OnPut("/", document);
  }
  EXPECT_TRUE(called);
  EXPECT_EQ(CloudDeviceSet::Status::ERASED, status);
}

TEST_F(CloudDeviceSetImplTest, WatchUnwatchOnDelete) {
  {
    CloudDeviceSetImpl short_lived_checker(InitFirebase());

    short_lived_checker.WatchFingerprint("some-token", "some-fingerprint",
                                         [](auto status) {});
    EXPECT_EQ(0, firebase_->unwatch_calls);
  }
  EXPECT_EQ(1, firebase_->unwatch_calls);
}

TEST_F(CloudDeviceSetImplTest, EraseAllFingerprints) {
  bool called;
  CloudDeviceSet::Status status;
  cloud_device_set_.EraseAllFingerprints(
      "some-token",
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(CloudDeviceSet::Status::OK, status);
  EXPECT_EQ((std::vector<std::vector<std::string>>{{"auth=some-token"}}),
            firebase_->delete_query_params);
  EXPECT_EQ((std::vector<std::string>{kDeviceMapRelpath}),
            firebase_->delete_keys);
}

}  // namespace
}  // namespace cloud_provider_firebase
