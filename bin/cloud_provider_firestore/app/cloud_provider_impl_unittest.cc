// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firestore/app/cloud_provider_impl.h"

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fxl/macros.h>
#include <lib/gtest/test_loop_fixture.h>

#include "peridot/bin/cloud_provider_firestore/firestore/testing/test_firestore_service.h"
#include "peridot/lib/convert/convert.h"
#include "peridot/lib/firebase_auth/testing/test_firebase_auth.h"

namespace cloud_provider_firestore {

class CloudProviderImplTest : public gtest::TestLoopFixture {
 public:
  CloudProviderImplTest() {
    auto firebase_auth =
        std::make_unique<firebase_auth::TestFirebaseAuth>(dispatcher());
    firebase_auth_ = firebase_auth.get();
    auto firestore_service = std::make_unique<TestFirestoreService>();
    firestore_service_ = firestore_service.get();
    cloud_provider_impl_ = std::make_unique<CloudProviderImpl>(
        "some user id", std::move(firebase_auth), std::move(firestore_service),
        cloud_provider_.NewRequest());
  }
  ~CloudProviderImplTest() override {}

 protected:
  firebase_auth::TestFirebaseAuth* firebase_auth_ = nullptr;

  TestFirestoreService* firestore_service_;
  cloud_provider::CloudProviderPtr cloud_provider_;
  std::unique_ptr<CloudProviderImpl> cloud_provider_impl_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(CloudProviderImplTest);
};

TEST_F(CloudProviderImplTest, EmptyWhenClientDisconnected) {
  bool on_empty_called = false;
  cloud_provider_impl_->set_on_empty(
      [this, &on_empty_called] { on_empty_called = true; });
  EXPECT_FALSE(firestore_service_->shutdown_callback);
  cloud_provider_.Unbind();
  RunLoopUntilIdle();

  // Verify that shutdown was started, but on_empty wasn't called yet.
  EXPECT_TRUE(firestore_service_->shutdown_callback);
  EXPECT_FALSE(on_empty_called);

  // Verify that on_empty is called when the shutdown callback is executed.
  firestore_service_->shutdown_callback();
  EXPECT_TRUE(on_empty_called);
}

TEST_F(CloudProviderImplTest, EmptyWhenFirebaseAuthDisconnected) {
  bool on_empty_called = false;
  cloud_provider_impl_->set_on_empty(
      [this, &on_empty_called] { on_empty_called = true; });
  firebase_auth_->TriggerConnectionErrorHandler();
  RunLoopUntilIdle();

  // Verify that shutdown was started, but on_empty wasn't called yet.
  EXPECT_TRUE(firestore_service_->shutdown_callback);
  EXPECT_FALSE(on_empty_called);

  // Verify that on_empty is called when the shutdown callback is executed.
  firestore_service_->shutdown_callback();
  EXPECT_TRUE(on_empty_called);
}

TEST_F(CloudProviderImplTest, GetDeviceSet) {
  bool callback_called = false;
  auto status = cloud_provider::Status::INTERNAL_ERROR;
  cloud_provider::DeviceSetPtr device_set;
  cloud_provider_->GetDeviceSet(
      device_set.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&callback_called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(cloud_provider::Status::OK, status);

  // Expect one placeholder document creation request: root document of the
  // serialization version.
  EXPECT_EQ(1u, firestore_service_->create_document_records.size());
}

TEST_F(CloudProviderImplTest, GetPageCloud) {
  bool callback_called = false;
  auto status = cloud_provider::Status::INTERNAL_ERROR;
  cloud_provider::PageCloudPtr page_cloud;
  cloud_provider_->GetPageCloud(
      convert::ToArray("app_id"), convert::ToArray("page_id"),
      page_cloud.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&callback_called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(cloud_provider::Status::OK, status);

  // Expect three placeholder document creation request: root document of the
  // serialization version, root document of the app namespace, root document of
  // the page.
  EXPECT_EQ(3u, firestore_service_->create_document_records.size());
}

}  // namespace cloud_provider_firestore
