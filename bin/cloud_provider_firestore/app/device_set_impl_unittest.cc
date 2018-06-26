// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firestore/app/device_set_impl.h"

#include <fuchsia/ledger/cloud/cpp/fidl.h>

#include "lib/callback/capture.h"
#include "lib/callback/set_when_called.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/macros.h"
#include "lib/gtest/test_loop_fixture.h"
#include "peridot/bin/cloud_provider_firestore/app/testing/test_credentials_provider.h"
#include "peridot/bin/cloud_provider_firestore/firestore/testing/test_firestore_service.h"
#include "peridot/lib/convert/convert.h"

namespace cloud_provider_firestore {
namespace {

class DeviceSetImplTest : public gtest::TestLoopFixture,
                          public cloud_provider::DeviceSetWatcher {
 public:
  DeviceSetImplTest()
      : test_credentials_provider_(dispatcher()),
        device_set_impl_("user_path", &test_credentials_provider_,
                         &firestore_service_, device_set_.NewRequest()),
        watcher_binding_(this) {}

  // cloud_provider::DeviceSetWatcher:
  void OnCloudErased() override { on_cloud_erased_calls_++; }

  void OnNetworkError() override { on_network_error_calls_++; }

 protected:
  cloud_provider::DeviceSetPtr device_set_;
  TestCredentialsProvider test_credentials_provider_;
  TestFirestoreService firestore_service_;
  DeviceSetImpl device_set_impl_;

  fidl::Binding<cloud_provider::DeviceSetWatcher> watcher_binding_;
  int on_cloud_erased_calls_ = 0;
  int on_network_error_calls_ = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(DeviceSetImplTest);
};

TEST_F(DeviceSetImplTest, EmptyWhenDisconnected) {
  bool on_empty_called = false;
  device_set_impl_.set_on_empty(callback::SetWhenCalled(&on_empty_called));
  device_set_.Unbind();
  RunLoopUntilIdle();
  EXPECT_TRUE(on_empty_called);
}

TEST_F(DeviceSetImplTest, CheckFingerprintOk) {
  bool callback_called = false;
  auto status = cloud_provider::Status::INTERNAL_ERROR;
  device_set_->CheckFingerprint(
      convert::ToArray("abc"),
      callback::Capture(callback::SetWhenCalled(&callback_called), &status));

  RunLoopUntilIdle();
  EXPECT_FALSE(callback_called);
  EXPECT_EQ(1u, firestore_service_.get_document_records.size());

  firestore_service_.get_document_records.front().callback(
      grpc::Status(), google::firestore::v1beta1::Document());

  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(cloud_provider::Status::OK, status);
}

TEST_F(DeviceSetImplTest, CheckFingerprintNotFound) {
  bool callback_called = false;
  auto status = cloud_provider::Status::INTERNAL_ERROR;
  device_set_->CheckFingerprint(
      convert::ToArray("abc"),
      callback::Capture(callback::SetWhenCalled(&callback_called), &status));

  RunLoopUntilIdle();
  EXPECT_FALSE(callback_called);
  EXPECT_EQ(1u, firestore_service_.get_document_records.size());

  firestore_service_.get_document_records.front().callback(
      grpc::Status(grpc::NOT_FOUND, ""),
      google::firestore::v1beta1::Document());

  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(cloud_provider::Status::NOT_FOUND, status);
}

TEST_F(DeviceSetImplTest, SetFingerprint) {
  bool callback_called = false;
  auto status = cloud_provider::Status::INTERNAL_ERROR;
  device_set_->SetFingerprint(
      convert::ToArray("abc"),
      callback::Capture(callback::SetWhenCalled(&callback_called), &status));

  RunLoopUntilIdle();
  EXPECT_FALSE(callback_called);
  EXPECT_EQ(1u, firestore_service_.create_document_records.size());

  firestore_service_.create_document_records.front().callback(
      grpc::Status(), google::firestore::v1beta1::Document());

  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
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

TEST_F(DeviceSetImplTest, Erase) {
  bool callback_called = false;
  auto status = cloud_provider::Status::INTERNAL_ERROR;
  device_set_->Erase(
      callback::Capture(callback::SetWhenCalled(&callback_called), &status));

  RunLoopUntilIdle();
  EXPECT_FALSE(callback_called);
  EXPECT_EQ(1u, firestore_service_.list_documents_records.size());

  auto response = google::firestore::v1beta1::ListDocumentsResponse();
  response.add_documents()->set_name("some/document/name");
  response.add_documents()->set_name("some/other/name");
  firestore_service_.list_documents_records[0].callback(grpc::Status::OK,
                                                        std::move(response));

  RunLoopUntilIdle();
  EXPECT_FALSE(callback_called);
  EXPECT_EQ(2u, firestore_service_.delete_document_records.size());
  EXPECT_EQ("some/document/name",
            firestore_service_.delete_document_records[0].request.name());
  EXPECT_EQ("some/other/name",
            firestore_service_.delete_document_records[1].request.name());

  firestore_service_.delete_document_records[0].callback(grpc::Status::OK);
  firestore_service_.delete_document_records[1].callback(grpc::Status::OK);

  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(cloud_provider::Status::OK, status);
}

// Paginated response from the device map list is not currently handled - we
// give up and return INTERNAL_ERROR. When we add support for pagination, this
// test should be edited to verify the correct behavior.
TEST_F(DeviceSetImplTest, EraseWithPaginatedDeviceListResponse) {
  bool callback_called = false;
  auto status = cloud_provider::Status::OK;
  device_set_->Erase(
      callback::Capture(callback::SetWhenCalled(&callback_called), &status));

  RunLoopUntilIdle();
  EXPECT_FALSE(callback_called);
  EXPECT_EQ(1u, firestore_service_.list_documents_records.size());

  auto response = google::firestore::v1beta1::ListDocumentsResponse();
  response.add_documents()->set_name("some/document/name");
  response.add_documents()->set_name("some/other/name");
  response.set_next_page_token("token");
  firestore_service_.list_documents_records[0].callback(grpc::Status::OK,
                                                        std::move(response));

  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(cloud_provider::Status::INTERNAL_ERROR, status);
}

}  // namespace
}  // namespace cloud_provider_firestore
