// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <grpc++/grpc++.h>
#include <gtest/gtest.h>
#include <src/lib/fxl/logging.h>

#include "src/virtualization/lib/guest_interaction/client/client_operation_state.h"
#include "test_lib.h"

// Client Get State Machine Test Cases
//
// 1. Client requests a file that does not exist on the server.
// 2. Client requests a file smaller than the fragmentation size.
// 3. Client requests a file larger than the fragmentation size.
// 4. Client fails to open the copy of the file.
// 5. Client fails to write to the copy of the file.
// 6. Server immediately hangs up on client at start of transfer.

TEST_F(AsyncEndToEndTest, GetMissingFile) {
  ResetStub();
  // Accounting bits for managing CompletionQueue state.
  void* tag;
  bool cq_status;

  // Create a service that can accept incoming Get requests.
  GetRequest incoming_request;
  grpc::ServerContext srv_ctx;
  grpc::ServerAsyncWriter<GetResponse> response_writer(&srv_ctx);

  service_->RequestGet(&srv_ctx, &incoming_request, &response_writer, server_cq_.get(),
                       server_cq_.get(), this);

  // Create components required to perform a client Get request.
  OperationStatus operation_status = OperationStatus::OK;
  GetRequest get_request;
  get_request.set_source("/some/bogus/path");

  uint32_t fake_fd = 0;
  GetCallData<FakePlatform>* client_call_data = new GetCallData<FakePlatform>(
      fake_fd, [&operation_status](OperationStatus status) { operation_status = status; });

  client_call_data->reader_ =
      stub_->AsyncGet(&(client_call_data->ctx_), get_request, client_cq_.get(), client_call_data);

  // Wait for the request to go out.
  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(cq_status);

  // Server CompletionQueue should get the client request.
  server_cq_->Next(&tag, &cq_status);

  GetResponse outgoing_response;
  outgoing_response.clear_data();
  outgoing_response.set_status(OperationStatus::SERVER_MISSING_FILE_FAILURE);
  response_writer.Write(outgoing_response, nullptr);

  // Client should get the server's message and then wait for the server to
  // call Finish.
  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(cq_status);

  // Server finishes.
  server_cq_->Next(&tag, &cq_status);
  response_writer.Finish(grpc::Status::OK, nullptr);

  // Client gets final status from server, runs the callback, and then
  // deletes itself.
  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(cq_status);

  // The client sets the operation status in the callback.
  ASSERT_EQ(operation_status, OperationStatus::SERVER_MISSING_FILE_FAILURE);
}

TEST_F(AsyncEndToEndTest, SmallFile) {
  ResetStub();
  // Accounting bits for managing CompletionQueue state.
  void* tag;
  bool cq_status;

  // Create a service that can accept incoming Get requests.
  GetRequest incoming_request;
  grpc::ServerContext srv_ctx;
  grpc::ServerAsyncWriter<GetResponse> response_writer(&srv_ctx);

  service_->RequestGet(&srv_ctx, &incoming_request, &response_writer, server_cq_.get(),
                       server_cq_.get(), this);

  // Create components required to perform a client Get request.
  OperationStatus operation_status = OperationStatus::GRPC_FAILURE;
  GetRequest get_request;
  get_request.set_source("/some/small/file");

  uint32_t fake_fd = 0;
  GetCallData<FakePlatform>* client_call_data = new GetCallData<FakePlatform>(
      fake_fd, [&operation_status](OperationStatus status) { operation_status = status; });
  client_call_data->reader_ =
      stub_->AsyncGet(&(client_call_data->ctx_), get_request, client_cq_.get(), client_call_data);

  // Wait for the request to go out.
  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(cq_status);

  // The mock will notify the client that all writes are successful.
  client_call_data->platform_interface_.SetOpenFileReturn(1);
  client_call_data->platform_interface_.SetWriteFileReturn(1);

  // Server CompletionQueue should get the client request.
  // Send back a short message.
  server_cq_->Next(&tag, &cq_status);

  GetResponse outgoing_response;
  outgoing_response.set_data("Small file contents");
  outgoing_response.set_status(OperationStatus::OK);
  response_writer.Write(outgoing_response, nullptr);

  // Client should get the server's message and then wait for the server to
  // send more data.
  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(cq_status);

  // Server finishes, indicating that there is no more data.
  server_cq_->Next(&tag, &cq_status);
  response_writer.Finish(grpc::Status::OK, nullptr);

  // Client should get a bad status from the queue and then wait for the query
  // of the finish status.
  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(cq_status);

  // Client gets final status, runs the callback, and then deletes itself.
  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(cq_status);

  // The client sets the operation status in the callback.
  ASSERT_EQ(operation_status, OperationStatus::OK);
}

TEST_F(AsyncEndToEndTest, LargeFile) {
  ResetStub();
  // Accounting bits for managing CompletionQueue state.
  void* tag;
  bool cq_status;

  // Create a service that can accept incoming Get requests.
  GetRequest incoming_request;
  grpc::ServerContext srv_ctx;
  grpc::ServerAsyncWriter<GetResponse> response_writer(&srv_ctx);

  service_->RequestGet(&srv_ctx, &incoming_request, &response_writer, server_cq_.get(),
                       server_cq_.get(), this);

  // Create components required to perform a client Get request.
  OperationStatus operation_status = OperationStatus::GRPC_FAILURE;
  GetRequest get_request;
  get_request.set_source("/some/small/file");

  uint32_t fake_fd = 0;
  GetCallData<FakePlatform>* client_call_data = new GetCallData<FakePlatform>(
      fake_fd, [&operation_status](OperationStatus status) { operation_status = status; });
  client_call_data->reader_ =
      stub_->AsyncGet(&(client_call_data->ctx_), get_request, client_cq_.get(), client_call_data);

  // Wait for the request to go out.
  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(cq_status);

  // The mock will notify the client that all writes are successful.
  client_call_data->platform_interface_.SetOpenFileReturn(1);
  client_call_data->platform_interface_.SetWriteFileReturn(1);

  // Server CompletionQueue should get the client request.
  // Send back a short message.
  server_cq_->Next(&tag, &cq_status);

  GetResponse outgoing_response;
  outgoing_response.set_data("large file contents");
  outgoing_response.set_status(OperationStatus::OK);
  response_writer.Write(outgoing_response, nullptr);

  // Client should get the server's message and then wait for the server to
  // send more data.
  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(cq_status);

  // Server CompletionQueue should get the client request.
  // Send back a short message.
  server_cq_->Next(&tag, &cq_status);

  outgoing_response.set_data("large file contents");
  outgoing_response.set_status(OperationStatus::OK);
  response_writer.Write(outgoing_response, nullptr);

  // Client should get the server's message and then wait for the server to
  // send more data.
  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(cq_status);

  // Server finishes, indicating that there is no more data.
  server_cq_->Next(&tag, &cq_status);
  response_writer.Finish(grpc::Status::OK, nullptr);

  // Client should get a bad status from the queue and then wait for the query
  // the finish status.
  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(cq_status);

  // Client gets final status, runs the callback, and then deletes itself.
  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(cq_status);

  // The client sets the operation status in the callback.
  ASSERT_EQ(operation_status, OperationStatus::OK);
}

TEST_F(AsyncEndToEndTest, BrokenWrite) {
  ResetStub();
  // Accounting bits for managing CompletionQueue state.
  void* tag;
  bool cq_status;

  // Create a service that can accept incoming Get requests.
  GetRequest incoming_request;
  grpc::ServerContext srv_ctx;
  grpc::ServerAsyncWriter<GetResponse> response_writer(&srv_ctx);

  service_->RequestGet(&srv_ctx, &incoming_request, &response_writer, server_cq_.get(),
                       server_cq_.get(), this);

  // Create components required to perform a client Get request.
  OperationStatus operation_status = OperationStatus::GRPC_FAILURE;
  GetRequest get_request;
  get_request.set_source("/some/small/file");

  uint32_t fake_fd = 0;
  GetCallData<FakePlatform>* client_call_data = new GetCallData<FakePlatform>(
      fake_fd, [&operation_status](OperationStatus status) { operation_status = status; });
  client_call_data->reader_ =
      stub_->AsyncGet(&(client_call_data->ctx_), get_request, client_cq_.get(), client_call_data);

  // Wait for the request to go out.
  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(cq_status);

  // The mock will notify the client that write has failed.
  client_call_data->platform_interface_.SetOpenFileReturn(1);
  client_call_data->platform_interface_.SetOpenFileReturn(1);
  client_call_data->platform_interface_.SetWriteFileReturn(-1);

  // Server CompletionQueue should get the client request.
  // Send back a short message.
  server_cq_->Next(&tag, &cq_status);

  GetResponse outgoing_response;
  outgoing_response.set_data("Small file contents");
  outgoing_response.set_status(OperationStatus::OK);
  response_writer.Write(outgoing_response, nullptr);

  // Client should get the server's message, fail to write, and then finish.
  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(cq_status);

  // Server finishes, indicating that there is no more data.
  server_cq_->Next(&tag, &cq_status);
  response_writer.Finish(grpc::Status::OK, nullptr);

  // Client finishes and deletes itself.
  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(cq_status);

  // The client sets the operation status in the callback.
  ASSERT_EQ(operation_status, OperationStatus::CLIENT_FILE_WRITE_FAILURE);
}

TEST_F(AsyncEndToEndTest, GrpcFailure) {
  ResetStub();
  // Accounting bits for managing CompletionQueue state.
  void* tag;
  bool cq_status;

  // Create a service that can accept incoming Get requests.
  GetRequest incoming_request;
  grpc::ServerContext srv_ctx;
  grpc::ServerAsyncWriter<GetResponse> response_writer(&srv_ctx);

  service_->RequestGet(&srv_ctx, &incoming_request, &response_writer, server_cq_.get(),
                       server_cq_.get(), this);

  // Create components required to perform a client Get request.
  OperationStatus operation_status = OperationStatus::GRPC_FAILURE;
  GetRequest get_request;
  get_request.set_source("/some/small/file");

  uint32_t fake_fd = 0;
  GetCallData<FakePlatform>* client_call_data = new GetCallData<FakePlatform>(
      fake_fd, [&operation_status](OperationStatus status) { operation_status = status; });
  client_call_data->reader_ =
      stub_->AsyncGet(&(client_call_data->ctx_), get_request, client_cq_.get(), client_call_data);

  // Wait for the request to go out and then tell the client that it was
  // unsuccessful.
  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(false);

  // Server finishes, indicating that there is no more data.
  server_cq_->Next(&tag, &cq_status);
  response_writer.Finish(grpc::Status::OK, nullptr);

  // Client finishes and deletes itself.
  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(cq_status);

  // The client sets the operation status in the callback.
  ASSERT_EQ(operation_status, OperationStatus::GRPC_FAILURE);
}
