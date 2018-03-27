// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firestore/app/page_cloud_impl.h"

#include "garnet/lib/callback/capture.h"
#include "garnet/lib/callback/set_when_called.h"
#include "garnet/lib/gtest/test_with_message_loop.h"
#include "lib/cloud_provider/fidl/cloud_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fsl/socket/strings.h"
#include "lib/fsl/vmo/sized_vmo.h"
#include "lib/fsl/vmo/strings.h"
#include "peridot/bin/cloud_provider_firestore/app/testing/test_credentials_provider.h"
#include "peridot/bin/cloud_provider_firestore/firestore/testing/test_firestore_service.h"
#include "peridot/lib/convert/convert.h"

namespace cloud_provider_firestore {
namespace {

class PageCloudImplTest : public gtest::TestWithMessageLoop {
 public:
  PageCloudImplTest()
      : test_credentials_provider_(message_loop_.task_runner()),
        page_cloud_impl_("page_path",
                         &test_credentials_provider_,
                         &firestore_service_,
                         page_cloud_.NewRequest()) {}
  ~PageCloudImplTest() override {}

 protected:
  cloud_provider::PageCloudPtr page_cloud_;
  TestCredentialsProvider test_credentials_provider_;
  TestFirestoreService firestore_service_;
  PageCloudImpl page_cloud_impl_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageCloudImplTest);
};

TEST_F(PageCloudImplTest, EmptyWhenDisconnected) {
  bool on_empty_called = false;
  page_cloud_impl_.set_on_empty([this, &on_empty_called] {
    on_empty_called = true;
    message_loop_.PostQuitTask();
  });
  page_cloud_.Unbind();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_TRUE(on_empty_called);
}

TEST_F(PageCloudImplTest, AddObject) {
  bool callback_called = false;
  auto status = cloud_provider::Status::INTERNAL_ERROR;
  fsl::SizedVmo data;
  ASSERT_TRUE(fsl::VmoFromString("some_data", &data));
  page_cloud_->AddObject(
      convert::ToArray("some_id"), std::move(data).ToTransport(),
      callback::Capture(callback::SetWhenCalled(&callback_called), &status));

  RunLoopUntilIdle();
  EXPECT_FALSE(callback_called);
  EXPECT_EQ(1u, firestore_service_.create_document_records.size());

  firestore_service_.create_document_records.front().callback(
      grpc::Status::OK, google::firestore::v1beta1::Document());

  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(cloud_provider::Status::OK, status);
}

TEST_F(PageCloudImplTest, GetObject) {
  bool callback_called = false;
  auto status = cloud_provider::Status::INTERNAL_ERROR;
  uint64_t size;
  zx::socket data;
  page_cloud_->GetObject(
      convert::ToArray("some_id"),
      callback::Capture(callback::SetWhenCalled(&callback_called), &status,
                        &size, &data));

  RunLoopUntilIdle();
  EXPECT_FALSE(callback_called);
  EXPECT_EQ(1u, firestore_service_.get_document_records.size());

  const std::string response_data = "some_data";
  google::firestore::v1beta1::Document response;
  *((*response.mutable_fields())["data"].mutable_bytes_value()) = response_data;
  firestore_service_.get_document_records.front().callback(grpc::Status(),
                                                           response);

  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(cloud_provider::Status::OK, status);
  EXPECT_EQ(response_data.size(), size);
  EXPECT_TRUE(data);

  std::string read_data;
  ASSERT_TRUE(fsl::BlockingCopyToString(std::move(data), &read_data));
  EXPECT_EQ("some_data", read_data);
}

}  // namespace
}  // namespace cloud_provider_firestore
