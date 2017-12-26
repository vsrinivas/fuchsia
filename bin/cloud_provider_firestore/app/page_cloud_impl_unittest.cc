// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firestore/app/page_cloud_impl.h"

#include "lib/cloud_provider/fidl/cloud_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "peridot/lib/gtest/test_with_message_loop.h"

namespace cloud_provider_firestore {
namespace {

class PageCloudImplTest : public gtest::TestWithMessageLoop {
 public:
  PageCloudImplTest() : page_cloud_impl_(page_cloud_.NewRequest()) {}
  ~PageCloudImplTest() override {}

 protected:
  cloud_provider::PageCloudPtr page_cloud_;
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
  page_cloud_.reset();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_TRUE(on_empty_called);
}

}  // namespace
}  // namespace cloud_provider_firestore
