// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firestore/app/device_set_impl.h"

#include "lib/cloud_provider/fidl/cloud_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/cloud_provider_firestore/firestore/testing/test_firestore_service.h"
#include "peridot/lib/convert/convert.h"
#include "peridot/lib/gtest/test_with_message_loop.h"

namespace cloud_provider_firestore {
namespace {

class DeviceSetImplTest : public gtest::TestWithMessageLoop {
 public:
  DeviceSetImplTest()
      : device_set_impl_("user_path",
                         &firestore_service_,
                         device_set_.NewRequest()) {
    // Configure test Firestore service to quit the message loop at each
    // request.
    firestore_service_.SetOnRequest([this] { message_loop_.PostQuitTask(); });
  }

 protected:
  cloud_provider::DeviceSetPtr device_set_;
  TestFirestoreService firestore_service_;
  DeviceSetImpl device_set_impl_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(DeviceSetImplTest);
};

TEST_F(DeviceSetImplTest, EmptyWhenDisconnected) {
  bool on_empty_called = false;
  device_set_impl_.set_on_empty([this, &on_empty_called] {
    on_empty_called = true;
    message_loop_.PostQuitTask();
  });
  device_set_.reset();
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

}  // namespace
}  // namespace cloud_provider_firestore
