// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firestore/firestore/listen_call.h"

#include <lib/fit/function.h>

#include "gtest/gtest.h"

namespace cloud_provider_firestore {

class TestListenStream : public ListenStream {
 public:
  TestListenStream() {}
  ~TestListenStream() override {}

  // ListenStream:
  void StartCall(void* tag) override {
    connect_tag = static_cast<fit::function<void(bool)>*>(tag);
  }
  void ReadInitialMetadata(void* /*tag*/) override {}
  void Read(google::firestore::v1beta1::ListenResponse* /*response*/,
            void* tag) override {
    read_tag = static_cast<fit::function<void(bool)>*>(tag);
  }
  void Write(const google::firestore::v1beta1::ListenRequest& /*request*/,
             void* tag) override {
    write_tag = static_cast<fit::function<void(bool)>*>(tag);
  }
  void Write(const google::firestore::v1beta1::ListenRequest& /*request*/,
             grpc::WriteOptions /*options*/, void* /*tag*/) override {}
  void WritesDone(void* /*tag*/) override {}
  void Finish(grpc::Status* /*status*/, void* tag) override {
    finish_tag = static_cast<fit::function<void(bool)>*>(tag);
  }

  fit::function<void(bool)>* connect_tag = nullptr;
  fit::function<void(bool)>* read_tag = nullptr;
  fit::function<void(bool)>* write_tag = nullptr;
  fit::function<void(bool)>* finish_tag = nullptr;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(TestListenStream);
};

class ListenCallTest : public ::testing::Test, public ListenCallClient {
 public:
  ListenCallTest() {
    auto stream = std::make_unique<TestListenStream>();
    auto context = std::make_unique<grpc::ClientContext>();
    stream_ = stream.get();
    call_ = std::make_unique<ListenCall>(this, std::move(context),
                                         std::move(stream));
    call_->set_on_empty([this] { on_empty_calls_++; });
  }
  ~ListenCallTest() override {}

  // ListenCallClient:
  void OnConnected() override { on_connected_calls_++; }

  void OnResponse(
      google::firestore::v1beta1::ListenResponse /*response*/) override {
    on_response_calls_++;
  }

  void OnFinished(grpc::Status status) override {
    on_finished_calls_++;
    status_ = status;
  }

 protected:
  TestListenStream* stream_;
  std::unique_ptr<ListenCall> call_;

  int on_connected_calls_ = 0;
  int on_response_calls_ = 0;
  int on_finished_calls_ = 0;
  grpc::Status status_;

  int on_empty_calls_ = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(ListenCallTest);
};

TEST_F(ListenCallTest, DeleteHandlerBeforeConnect) {
  auto handler = std::make_unique<ListenCallHandlerImpl>(call_.get());
  handler.reset();

  // Simulate the connection response arriving.
  (*stream_->connect_tag)(true);

  // Verify that on_empty was called.
  EXPECT_EQ(1, on_empty_calls_);

  // Verify that no client calls were made (because the handler was deleted at
  // the very beginning and no client calls should be made after it goes away).
  EXPECT_EQ(0, on_connected_calls_);
  EXPECT_EQ(0, on_response_calls_);
  EXPECT_EQ(0, on_finished_calls_);
}

TEST_F(ListenCallTest, ConnectionError) {
  auto handler = std::make_unique<ListenCallHandlerImpl>(call_.get());
  (*stream_->connect_tag)(false);
  EXPECT_EQ(0, on_connected_calls_);
  EXPECT_EQ(0, on_response_calls_);
  EXPECT_EQ(1, on_finished_calls_);
  EXPECT_FALSE(status_.ok());
}

TEST_F(ListenCallTest, DeleteHandlerAfterConnect) {
  auto handler = std::make_unique<ListenCallHandlerImpl>(call_.get());
  (*stream_->connect_tag)(true);
  EXPECT_EQ(1, on_connected_calls_);

  // Delete the handler and simulate the pending read call failing due to being
  // interrupted.
  handler.reset();
  (*stream_->read_tag)(false);

  // Verify that on_empty was called.
  EXPECT_EQ(1, on_empty_calls_);

  // Verify that no further client calls were made after the handler is deleted.
  EXPECT_EQ(1, on_connected_calls_);
  EXPECT_EQ(0, on_response_calls_);
  EXPECT_EQ(0, on_finished_calls_);
}

TEST_F(ListenCallTest, WriteAndDeleteHandler) {
  auto handler = std::make_unique<ListenCallHandlerImpl>(call_.get());
  (*stream_->connect_tag)(true);
  EXPECT_EQ(1, on_connected_calls_);

  handler->Write(google::firestore::v1beta1::ListenRequest());
  (*stream_->write_tag)(true);

  handler.reset();
  (*stream_->read_tag)(false);
  EXPECT_EQ(1, on_empty_calls_);
  EXPECT_EQ(1, on_connected_calls_);
  EXPECT_EQ(0, on_response_calls_);
  EXPECT_EQ(0, on_finished_calls_);
}

TEST_F(ListenCallTest, ReadAndDeleteHandler) {
  auto handler = std::make_unique<ListenCallHandlerImpl>(call_.get());
  (*stream_->connect_tag)(true);
  EXPECT_EQ(1, on_connected_calls_);

  (*stream_->read_tag)(true);
  (*stream_->read_tag)(true);
  (*stream_->read_tag)(true);

  handler.reset();
  (*stream_->read_tag)(false);
  EXPECT_EQ(1, on_empty_calls_);
  EXPECT_EQ(1, on_connected_calls_);
  EXPECT_EQ(3, on_response_calls_);
  EXPECT_EQ(0, on_finished_calls_);
}

TEST_F(ListenCallTest, ReadError) {
  auto handler = std::make_unique<ListenCallHandlerImpl>(call_.get());
  (*stream_->connect_tag)(true);
  EXPECT_EQ(1, on_connected_calls_);

  (*stream_->read_tag)(true);
  EXPECT_EQ(1, on_response_calls_);
  EXPECT_FALSE(stream_->finish_tag);

  // Simulate read error, verify that we attempt to finish the stream to
  // retrieve the error status.
  (*stream_->read_tag)(false);
  EXPECT_TRUE(stream_->finish_tag);

  (*stream_->finish_tag)(true);
  EXPECT_EQ(1, on_finished_calls_);
}

}  // namespace cloud_provider_firestore
