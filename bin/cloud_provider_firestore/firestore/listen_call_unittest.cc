// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firestore/firestore/listen_call.h"

#include "peridot/bin/ledger/test/test_with_message_loop.h"

namespace cloud_provider_firestore {

class TestListenStream : public ListenStream {
 public:
  TestListenStream() {}
  ~TestListenStream() override {}

  // ListenStream:
  void ReadInitialMetadata(void* tag) override {}
  void Read(google::firestore::v1beta1::ListenResponse* response,
            void* tag) override {
    read_tag = static_cast<std::function<void(bool)>*>(tag);
  }
  void Write(const google::firestore::v1beta1::ListenRequest& request,
             void* tag) override {
    write_tag = static_cast<std::function<void(bool)>*>(tag);
  }
  void Write(const google::firestore::v1beta1::ListenRequest& request,
             grpc::WriteOptions options,
             void* tag) override {}
  void WritesDone(void* tag) override {}
  void Finish(grpc::Status* status, void* tag) override {
    finish_tag = static_cast<std::function<void(bool)>*>(tag);
  }

  std::function<void(bool)>* read_tag = nullptr;
  std::function<void(bool)>* write_tag = nullptr;
  std::function<void(bool)>* finish_tag = nullptr;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(TestListenStream);
};

class ListenCallTest : public test::TestWithMessageLoop,
                       public ListenCallClient {
 public:
  ListenCallTest() {
    auto stream_factory = [this](grpc::ClientContext* context, void* tag) {
      connect_tag_ = static_cast<std::function<void(bool)>*>(tag);
      auto result = std::make_unique<TestListenStream>();
      stream_ = result.get();
      return result;
    };
    call_ = std::make_unique<ListenCall>(this, std::move(stream_factory));
    call_->set_on_empty([this] { on_empty_calls_++; });
  }
  ~ListenCallTest() override {}

  // ListenCallClient:
  void OnConnected() override { on_connected_calls_++; }

  void OnResponse(
      google::firestore::v1beta1::ListenResponse response) override {
    on_response_calls_++;
  }

  void OnFinished(grpc::Status status) override {
    on_finished_calls_++;
    status_ = status;
  }

 protected:
  TestListenStream* stream_;
  std::unique_ptr<ListenCall> call_;
  std::function<void(bool)>* connect_tag_ = nullptr;

  int on_connected_calls_ = 0;
  int on_response_calls_ = 0;
  int on_finished_calls_ = 0;
  grpc::Status status_;

  int on_empty_calls_ = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(ListenCallTest);
};

TEST_F(ListenCallTest, DeleteHandlerBeforeConnect) {
  auto handler = std::make_unique<ListenCallHandler>(call_.get());
  handler.reset();

  // Simulate the connection response arriving.
  (*connect_tag_)(true);

  // On empty should not yet be called - we still need to finish the stream.
  EXPECT_EQ(0, on_empty_calls_);

  // Simulate finishing the stream.
  (*stream_->finish_tag)(true);
  EXPECT_EQ(1, on_empty_calls_);

  // Verify that no client calls were made (because the handler was deleted at
  // the very beginning and no client calls should be made after it goes away).
  EXPECT_EQ(0, on_connected_calls_);
  EXPECT_EQ(0, on_response_calls_);
  EXPECT_EQ(0, on_finished_calls_);
}

TEST_F(ListenCallTest, ConnectionError) {
  auto handler = std::make_unique<ListenCallHandler>(call_.get());
  (*connect_tag_)(false);
  EXPECT_EQ(0, on_connected_calls_);
  EXPECT_EQ(0, on_response_calls_);
  EXPECT_EQ(1, on_finished_calls_);
  EXPECT_FALSE(status_.ok());
}

TEST_F(ListenCallTest, DeleteHandlerAfterConnect) {
  auto handler = std::make_unique<ListenCallHandler>(call_.get());
  (*connect_tag_)(true);
  EXPECT_EQ(1, on_connected_calls_);

  // Delete the handler and simulate the pending read call failing due to being
  // interrupted.
  handler.reset();
  (*stream_->read_tag)(false);

  // Simulate closing the stream.
  EXPECT_EQ(0, on_empty_calls_);
  (*stream_->finish_tag)(true);
  EXPECT_EQ(1, on_empty_calls_);

  // Verify that no further client calls were made after the handler is deleted.
  EXPECT_EQ(1, on_connected_calls_);
  EXPECT_EQ(0, on_response_calls_);
  EXPECT_EQ(0, on_finished_calls_);
}

TEST_F(ListenCallTest, WriteAndFinish) {
  auto handler = std::make_unique<ListenCallHandler>(call_.get());
  (*connect_tag_)(true);
  EXPECT_EQ(1, on_connected_calls_);

  handler->Write(google::firestore::v1beta1::ListenRequest());
  (*stream_->write_tag)(true);

  handler->Finish();
  (*stream_->read_tag)(false);
  (*stream_->finish_tag)(true);
  EXPECT_EQ(1, on_empty_calls_);
  EXPECT_EQ(1, on_connected_calls_);
  EXPECT_EQ(0, on_response_calls_);
  EXPECT_EQ(1, on_finished_calls_);
}

TEST_F(ListenCallTest, WriteAndDeleteHandler) {
  auto handler = std::make_unique<ListenCallHandler>(call_.get());
  (*connect_tag_)(true);
  EXPECT_EQ(1, on_connected_calls_);

  handler->Write(google::firestore::v1beta1::ListenRequest());
  (*stream_->write_tag)(true);

  handler.reset();
  (*stream_->read_tag)(false);
  (*stream_->finish_tag)(true);
  EXPECT_EQ(1, on_empty_calls_);
  EXPECT_EQ(1, on_connected_calls_);
  EXPECT_EQ(0, on_response_calls_);
  EXPECT_EQ(0, on_finished_calls_);
}

TEST_F(ListenCallTest, ReadAndFinish) {
  auto handler = std::make_unique<ListenCallHandler>(call_.get());
  (*connect_tag_)(true);
  EXPECT_EQ(1, on_connected_calls_);

  (*stream_->read_tag)(true);
  (*stream_->read_tag)(true);
  (*stream_->read_tag)(true);

  handler->Finish();
  (*stream_->read_tag)(false);
  (*stream_->finish_tag)(true);
  EXPECT_EQ(1, on_empty_calls_);
  EXPECT_EQ(1, on_connected_calls_);
  EXPECT_EQ(3, on_response_calls_);
  EXPECT_EQ(1, on_finished_calls_);
}

TEST_F(ListenCallTest, ReadAndDeleteHandler) {
  auto handler = std::make_unique<ListenCallHandler>(call_.get());
  (*connect_tag_)(true);
  EXPECT_EQ(1, on_connected_calls_);

  (*stream_->read_tag)(true);
  (*stream_->read_tag)(true);
  (*stream_->read_tag)(true);

  handler.reset();
  (*stream_->read_tag)(false);
  (*stream_->finish_tag)(true);
  EXPECT_EQ(1, on_empty_calls_);
  EXPECT_EQ(1, on_connected_calls_);
  EXPECT_EQ(3, on_response_calls_);
  EXPECT_EQ(0, on_finished_calls_);
}

}  // namespace cloud_provider_firestore
