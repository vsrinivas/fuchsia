// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firestore/grpc/read_stream_drainer.h"

#include <functional>
#include <vector>

#include <grpc++/grpc++.h>
#include <grpc++/support/async_stream.h>
#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <lib/fit/function.h>

#include "gtest/gtest.h"

namespace cloud_provider_firestore {
namespace {

using IntegerStream = grpc::ClientAsyncReaderInterface<int>;

class TestIntegerStream : public IntegerStream {
 public:
  TestIntegerStream() {}
  ~TestIntegerStream() override {}

  // IntegerStream:
  void StartCall(void* tag) override {
    connect_tag = static_cast<fit::function<void(bool)>*>(tag);
  }
  void ReadInitialMetadata(void* /*tag*/) override {}
  void Read(int* message, void* tag) override {
    message_ptr = message;
    read_tag = static_cast<fit::function<void(bool)>*>(tag);
  }
  void Finish(grpc::Status* status, void* tag) override {
    status_ptr = status;
    finish_tag = static_cast<fit::function<void(bool)>*>(tag);
  }

  fit::function<void(bool)>* connect_tag = nullptr;
  fit::function<void(bool)>* read_tag = nullptr;
  fit::function<void(bool)>* finish_tag = nullptr;

  int* message_ptr = nullptr;
  grpc::Status* status_ptr = nullptr;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(TestIntegerStream);
};

class ReadStreamDrainerTest : public ::testing::Test {
 public:
  ReadStreamDrainerTest() {
    auto stream = std::make_unique<TestIntegerStream>();
    auto context = std::make_unique<grpc::ClientContext>();
    stream_ = stream.get();
    drainer_ = std::make_unique<ReadStreamDrainer<IntegerStream, int>>(
        std::move(context), std::move(stream));
    drainer_->set_on_empty([this] { on_empty_calls_++; });
  }
  ~ReadStreamDrainerTest() override {}

 protected:
  TestIntegerStream* stream_;
  std::unique_ptr<ReadStreamDrainer<IntegerStream, int>> drainer_;

  int on_empty_calls_ = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(ReadStreamDrainerTest);
};

TEST_F(ReadStreamDrainerTest, ConnectionError) {
  bool called = false;
  grpc::Status status;
  std::vector<int> result;
  drainer_->Drain(
      callback::Capture(callback::SetWhenCalled(&called), &status, &result));
  (*stream_->connect_tag)(false);
  (*stream_->status_ptr) = grpc::Status(grpc::StatusCode::INTERNAL, "");
  EXPECT_FALSE(called);
  (*stream_->finish_tag)(true);

  EXPECT_TRUE(called);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(grpc::StatusCode::INTERNAL, status.error_code());
  EXPECT_TRUE(result.empty());
}

TEST_F(ReadStreamDrainerTest, Ok) {
  bool called = false;
  grpc::Status status;
  std::vector<int> result;
  drainer_->Drain(
      callback::Capture(callback::SetWhenCalled(&called), &status, &result));
  (*stream_->connect_tag)(true);
  (*stream_->message_ptr) = 1;
  (*stream_->read_tag)(true);
  (*stream_->message_ptr) = 2;
  (*stream_->read_tag)(true);
  // Signal that there is no more messages to read.
  (*stream_->read_tag)(false);
  (*stream_->status_ptr) = grpc::Status::OK;
  EXPECT_FALSE(called);
  (*stream_->finish_tag)(true);

  EXPECT_TRUE(called);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ((std::vector<int>{1, 2}), result);
}

}  // namespace
}  // namespace cloud_provider_firestore
