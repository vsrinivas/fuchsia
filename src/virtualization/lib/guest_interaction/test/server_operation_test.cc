// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <grpc++/grpc++.h>
#include <grpc/support/log.h>
#include <gtest/gtest.h>
#include <src/lib/fxl/logging.h>

#include "src/virtualization/lib/guest_interaction/server/server_operation_state.h"
#include "test_lib.h"

// Server Get State Machine Test Cases
//
// 1. Requested file does not exist.
// 2. Server fails to open the requested file.
// 3. Requested file is below the fragmentation size.
// 4. Requested file is above the fragmentation size.
// 5. The file the server is reading from goes into a bad state.

TEST_F(AsyncEndToEndTest, ServerMissingFile) {
  ResetStub();
  // Accounting bits for managing CompletionQueue state.
  void* tag;
  bool cq_status;

  // The server's call to check if the requested file exists will return false.
  GetCallData<FakePlatform>* server_call_data =
      new GetCallData<FakePlatform>(service_.get(), server_cq_.get());

  server_call_data->platform_interface_.SetFileExistsReturn(false);

  // Create components required to perform a client Get request.
  grpc::ClientContext client_ctx_;
  grpc::Status grpc_status;
  OperationStatus op_status;
  GetResponse get_response;
  GetRequest get_request;
  get_request.set_source("/some/bogus/path");

  std::unique_ptr<grpc::ClientAsyncReader<GetResponse>> reader_ =
      stub_->AsyncGet(&client_ctx_, get_request, client_cq_.get(), nullptr);

  // Wait for the request to go out, and then request to read from the server.
  client_cq_->Next(&tag, &cq_status);
  reader_->Read(&get_response, nullptr);

  // Server CompletionQueue should get the client request and reply that the
  // requested file does not exist.
  server_cq_->Next(&tag, &cq_status);
  server_call_data->Proceed(cq_status);

  // Client should get the server's message and then wait for the server to
  // call Finish.
  client_cq_->Next(&tag, &cq_status);
  op_status = get_response.status();
  ASSERT_EQ(op_status, OperationStatus::SERVER_MISSING_FILE_FAILURE);
  reader_->Finish(&grpc_status, nullptr);

  // Server finishes.
  server_cq_->Next(&tag, &cq_status);
  server_call_data->Proceed(cq_status);

  // Client gets final status from server.
  client_cq_->Next(&tag, &cq_status);
  ASSERT_TRUE(grpc_status.ok());
}

TEST_F(AsyncEndToEndTest, ServerFileOpenFailure) {
  ResetStub();
  // Accounting bits for managing CompletionQueue state.
  void* tag;
  bool cq_status;

  // The server's call to check if the requested file exists will return false.
  GetCallData<FakePlatform>* server_call_data =
      new GetCallData<FakePlatform>(service_.get(), server_cq_.get());

  server_call_data->platform_interface_.SetFileExistsReturn(true);
  server_call_data->platform_interface_.SetOpenFileReturn(-1);

  // Create components required to perform a client Get request.
  grpc::ClientContext client_ctx_;
  grpc::Status grpc_status;
  OperationStatus op_status;
  GetResponse get_response;
  GetRequest get_request;
  get_request.set_source("/file/with/permissions/issues");

  std::unique_ptr<grpc::ClientAsyncReader<GetResponse>> reader_ =
      stub_->AsyncGet(&client_ctx_, get_request, client_cq_.get(), nullptr);

  // Wait for the request to go out, and then request to read from the server.
  client_cq_->Next(&tag, &cq_status);
  reader_->Read(&get_response, nullptr);

  // Server CompletionQueue should get the client request and reply that the
  // requested file does not exist.
  server_cq_->Next(&tag, &cq_status);
  server_call_data->Proceed(cq_status);

  // Client should get the server's message and then wait for the server to
  // call Finish.
  client_cq_->Next(&tag, &cq_status);
  op_status = get_response.status();
  ASSERT_EQ(op_status, OperationStatus::SERVER_FILE_READ_FAILURE);
  reader_->Finish(&grpc_status, nullptr);

  // Server finishes.
  server_cq_->Next(&tag, &cq_status);
  server_call_data->Proceed(cq_status);

  // Client gets final status from server.
  client_cq_->Next(&tag, &cq_status);
  ASSERT_TRUE(grpc_status.ok());
}

TEST_F(AsyncEndToEndTest, ServerUnfragmentedRead) {
  ResetStub();
  // Accounting bits for managing CompletionQueue state.
  void* tag;
  bool cq_status;

  // The server will be told that the file exists and that it was able to open
  // open it.  It will be notified that the file is not bad, and EOF will be
  // false the first time and then true the second.  The call to read from the
  // file will return empty.
  GetCallData<FakePlatform>* server_call_data =
      new GetCallData<FakePlatform>(service_.get(), server_cq_.get());

  server_call_data->platform_interface_.SetFileExistsReturn(true);
  server_call_data->platform_interface_.SetOpenFileReturn(1);
  server_call_data->platform_interface_.SetReadFileContents("test");

  // Create components required to perform a client Get request.
  grpc::ClientContext client_ctx_;
  grpc::Status grpc_status;
  OperationStatus op_status;
  GetResponse get_response;
  GetRequest get_request;
  get_request.set_source("/some/test/file");

  std::unique_ptr<grpc::ClientAsyncReader<GetResponse>> reader_ =
      stub_->AsyncGet(&client_ctx_, get_request, client_cq_.get(), nullptr);

  // Wait for the request to go out, and then request to read from the server.
  client_cq_->Next(&tag, &cq_status);
  reader_->Read(&get_response, nullptr);

  // Server CompletionQueue should get the client request, open the file, and
  // send back the first chunk of contents.
  server_cq_->Next(&tag, &cq_status);
  server_call_data->Proceed(cq_status);

  // Client should get the server's message and then wait for more data.
  client_cq_->Next(&tag, &cq_status);
  ASSERT_STREQ(get_response.data().c_str(), "test");
  op_status = get_response.status();
  reader_->Read(&get_response, nullptr);

  // The server should hit EOF and send an empty chunk of data back to the
  // client.
  server_call_data->platform_interface_.SetReadFileContents("");
  server_cq_->Next(&tag, &cq_status);
  server_call_data->Proceed(cq_status);

  client_cq_->Next(&tag, &cq_status);
  op_status = get_response.status();
  reader_->Read(&get_response, nullptr);

  // The server should then call Finish.
  server_cq_->Next(&tag, &cq_status);
  server_call_data->Proceed(cq_status);

  // The client read should fail and then it should finish.
  client_cq_->Next(&tag, &cq_status);
  ASSERT_FALSE(cq_status);
  reader_->Finish(&grpc_status, nullptr);

  // Client gets final status from server.
  client_cq_->Next(&tag, &cq_status);
  ASSERT_TRUE(grpc_status.ok());
}

TEST_F(AsyncEndToEndTest, ServerFragmentedRead) {
  ResetStub();
  // Accounting bits for managing CompletionQueue state.
  void* tag;
  bool cq_status;

  // The server will be told that the file exists and that it was able to open
  // open it.  It will be notified that the file is not bad, and EOF will be
  // false the first time and then true the second.  The call to read from the
  // file will return empty.
  GetCallData<FakePlatform>* server_call_data =
      new GetCallData<FakePlatform>(service_.get(), server_cq_.get());

  server_call_data->platform_interface_.SetFileExistsReturn(true);
  server_call_data->platform_interface_.SetOpenFileReturn(1);
  server_call_data->platform_interface_.SetReadFileContents("test");

  // Create components required to perform a client Get request.
  grpc::ClientContext client_ctx_;
  grpc::Status grpc_status;
  OperationStatus op_status;
  GetResponse get_response;
  GetRequest get_request;
  get_request.set_source("/some/test/file");

  std::unique_ptr<grpc::ClientAsyncReader<GetResponse>> reader_ =
      stub_->AsyncGet(&client_ctx_, get_request, client_cq_.get(), nullptr);

  // Wait for the request to go out, and then request to read from the server.
  client_cq_->Next(&tag, &cq_status);
  reader_->Read(&get_response, nullptr);

  // Server CompletionQueue should get the client request, open the file, and
  // send back the first chunk of contents.
  server_cq_->Next(&tag, &cq_status);
  server_call_data->Proceed(cq_status);

  // Client should get the server's message and then wait for more data.
  client_cq_->Next(&tag, &cq_status);
  ASSERT_EQ(get_response.data(), std::string("test"));
  op_status = get_response.status();
  reader_->Read(&get_response, nullptr);

  // Repeat the process.
  server_cq_->Next(&tag, &cq_status);
  server_call_data->Proceed(cq_status);

  client_cq_->Next(&tag, &cq_status);
  ASSERT_EQ(get_response.data(), std::string("test"));
  op_status = get_response.status();
  reader_->Read(&get_response, nullptr);

  // The server should hit EOF and send an empty chunk of data back to the
  // client.
  server_call_data->platform_interface_.SetReadFileContents("");
  server_cq_->Next(&tag, &cq_status);
  server_call_data->Proceed(cq_status);

  client_cq_->Next(&tag, &cq_status);
  op_status = get_response.status();
  reader_->Read(&get_response, nullptr);

  // The server should then call Finish.
  server_cq_->Next(&tag, &cq_status);
  server_call_data->Proceed(cq_status);

  // The client read should fail and then it should finish.
  client_cq_->Next(&tag, &cq_status);
  ASSERT_FALSE(cq_status);
  reader_->Finish(&grpc_status, nullptr);

  // Client gets final status from server.
  client_cq_->Next(&tag, &cq_status);
  ASSERT_TRUE(grpc_status.ok());
}

TEST_F(AsyncEndToEndTest, ServerBadFile) {
  ResetStub();
  // Accounting bits for managing CompletionQueue state.
  void* tag;
  bool cq_status;

  // The server will be told that the file exists and that it was able to open
  // open it.  When the server goes to read from the file, it will be informed
  // that the file is bad.
  GetCallData<FakePlatform>* server_call_data =
      new GetCallData<FakePlatform>(service_.get(), server_cq_.get());

  server_call_data->platform_interface_.SetFileExistsReturn(true);
  server_call_data->platform_interface_.SetOpenFileReturn(1);
  server_call_data->platform_interface_.SetReadFileReturn(-1);

  // Create components required to perform a client Get request.
  grpc::ClientContext client_ctx_;
  grpc::Status grpc_status;
  OperationStatus op_status;
  GetResponse get_response;
  GetRequest get_request;
  get_request.set_source("/some/test/file");

  std::unique_ptr<grpc::ClientAsyncReader<GetResponse>> reader_ =
      stub_->AsyncGet(&client_ctx_, get_request, client_cq_.get(), nullptr);

  // Wait for the request to go out and then request to read from the server.
  client_cq_->Next(&tag, &cq_status);
  reader_->Read(&get_response, nullptr);

  // Server CompletionQueue should get the client request, open the file, and
  // send back a response indicating the read failed.
  server_cq_->Next(&tag, &cq_status);
  server_call_data->Proceed(cq_status);

  // Client should get the server's message and then wait for more data.
  client_cq_->Next(&tag, &cq_status);
  op_status = get_response.status();
  reader_->Read(&get_response, nullptr);

  // The server should finish.
  server_cq_->Next(&tag, &cq_status);
  server_call_data->Proceed(cq_status);

  // The client will get back a bad status and finish.
  client_cq_->Next(&tag, &cq_status);
  ASSERT_FALSE(cq_status);
  op_status = get_response.status();
  ASSERT_EQ(op_status, OperationStatus::SERVER_FILE_READ_FAILURE);
  reader_->Finish(&grpc_status, nullptr);

  // Client gets final status from server.
  client_cq_->Next(&tag, &cq_status);
  ASSERT_TRUE(grpc_status.ok());
}
