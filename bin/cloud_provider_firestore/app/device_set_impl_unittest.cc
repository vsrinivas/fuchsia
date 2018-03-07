// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firestore/app/device_set_impl.h"

#include "garnet/lib/callback/capture.h"
#include "garnet/lib/callback/set_when_called.h"
#include "garnet/lib/gtest/test_with_message_loop.h"
#include "lib/cloud_provider/fidl/cloud_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/cloud_provider_firestore/app/testing/test_credentials_provider.h"
#include "peridot/bin/cloud_provider_firestore/firestore/testing/test_firestore_service.h"
#include "peridot/lib/convert/convert.h"

namespace cloud_provider_firestore {
namespace {

class DeviceSetImplTest : public gtest::TestWithMessageLoop,
                          public cloud_provider::DeviceSetWatcher {
 public:
  DeviceSetImplTest()
      : test_credentials_provider_(message_loop_.task_runner()),
        device_set_impl_("user_path",
                         &test_credentials_provider_,
                         &firestore_service_,
                         device_set_.NewRequest()),
        watcher_binding_(this) {
    // Configure test Firestore service to quit the message loop at each
    // request.
    firestore_service_.SetOnRequest([this] { message_loop_.PostQuitTask(); });
  }

  // cloud_provider::DeviceSetWatcher:
  void OnCloudErased() override {
    on_cloud_erased_calls_++;
    message_loop_.PostQuitTask();
  }

  void OnNetworkError() override {
    on_network_error_calls_++;
    message_loop_.PostQuitTask();
  }

 protected:
  cloud_provider::DeviceSetPtr device_set_;
  TestCredentialsProvider test_credentials_provider_;
  TestFirestoreService firestore_service_;
  DeviceSetImpl device_set_impl_;

  f1dl::Binding<cloud_provider::DeviceSetWatcher> watcher_binding_;
  int on_cloud_erased_calls_ = 0;
  int on_network_error_calls_ = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(DeviceSetImplTest);
};

TEST_F(DeviceSetImplTest, EmptyWhenDisconnected) {
  bool on_empty_called = false;
  device_set_impl_.set_on_empty([this, &on_empty_called] {
    on_empty_called = true;
    message_loop_.PostQuitTask();
  });
  device_set_.Unbind();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_TRUE(on_empty_called);
}

TEST_F(DeviceSetImplTest, CheckFingerprintOk) {
  auto status = cloud_provider::Status::INTERNAL_ERROR;
  device_set_->CheckFingerprint(convert::ToArray("abc"),
                                [this, &status](auto got_status) {
                                  status = got_status;
                                  message_loop_.PostQuitTask();
                                });

  // Will be quit by the firestore service on-request callback.
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1u, firestore_service_.get_document_records.size());
  firestore_service_.get_document_records.front().callback(
      grpc::Status(), google::firestore::v1beta1::Document());

  // Will be quit by the CheckFingerprint() callback;
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(cloud_provider::Status::OK, status);
}

TEST_F(DeviceSetImplTest, CheckFingerprintNotFound) {
  auto status = cloud_provider::Status::INTERNAL_ERROR;
  device_set_->CheckFingerprint(convert::ToArray("abc"),
                                [this, &status](auto got_status) {
                                  status = got_status;
                                  message_loop_.PostQuitTask();
                                });

  // Will be quit by the firestore service on-request callback.
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1u, firestore_service_.get_document_records.size());
  firestore_service_.get_document_records.front().callback(
      grpc::Status(grpc::NOT_FOUND, ""),
      google::firestore::v1beta1::Document());

  // Will be quit by the CheckFingerprint() callback;
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(cloud_provider::Status::NOT_FOUND, status);
}

TEST_F(DeviceSetImplTest, SetFingerprint) {
  auto status = cloud_provider::Status::INTERNAL_ERROR;
  device_set_->SetFingerprint(convert::ToArray("abc"),
                              [this, &status](auto got_status) {
                                status = got_status;
                                message_loop_.PostQuitTask();
                              });

  // Will be quit by the firestore service on-request callback.
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1u, firestore_service_.create_document_records.size());
  firestore_service_.create_document_records.front().callback(
      grpc::Status(), google::firestore::v1beta1::Document());

  // Will be quit by the CheckFingerprint() callback;
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(cloud_provider::Status::OK, status);
}

TEST_F(DeviceSetImplTest, SetWatcherResultOk) {
  bool callback_called = false;
  auto status = cloud_provider::Status::INTERNAL_ERROR;
  cloud_provider::DeviceSetWatcherPtr watcher;
  watcher_binding_.Bind(watcher.NewRequest());
  device_set_->SetWatcher(
      convert::ToArray("abc"), std::move(watcher),
      callback::Capture(callback::SetWhenCalled(&callback_called), &status));

  RunLoopUntilIdle();
  EXPECT_EQ(1u, firestore_service_.listen_clients.size());
  EXPECT_FALSE(callback_called);

  auto response = google::firestore::v1beta1::ListenResponse();
  response.mutable_target_change()->set_target_change_type(
      google::firestore::v1beta1::TargetChange_TargetChangeType_CURRENT);
  firestore_service_.listen_clients[0]->OnResponse(std::move(response));

  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(cloud_provider::Status::OK, status);
  EXPECT_EQ(0, on_cloud_erased_calls_);
}

TEST_F(DeviceSetImplTest, SetWatcherResultCloudErased) {
  bool callback_called = false;
  auto status = cloud_provider::Status::INTERNAL_ERROR;
  cloud_provider::DeviceSetWatcherPtr watcher;
  watcher_binding_.Bind(watcher.NewRequest());
  device_set_->SetWatcher(
      convert::ToArray("abc"), std::move(watcher),
      callback::Capture(callback::SetWhenCalled(&callback_called), &status));

  RunLoopUntilIdle();
  EXPECT_EQ(1u, firestore_service_.listen_clients.size());
  EXPECT_FALSE(callback_called);

  auto response = google::firestore::v1beta1::ListenResponse();
  response.mutable_document_delete();
  firestore_service_.listen_clients[0]->OnResponse(std::move(response));

  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(cloud_provider::Status::NOT_FOUND, status);
  EXPECT_EQ(1, on_cloud_erased_calls_);
}

}  // namespace
}  // namespace cloud_provider_firestore
