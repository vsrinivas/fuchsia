// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <grpc/support/log.h>
#include <gtest/gtest.h>
#include <src/lib/fxl/logging.h>

#include "src/virtualization/lib/guest_interaction/server/server_operation_state.h"
#include "test_lib.h"

#include <grpc++/grpc++.h>

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

// Server Put State Machine Test
//
// 1. Destination is a directory.
// 2. Server fails to create destination directory.
// 3. Server fails to open destination file.
// 4. Server fails to write to the destination file.
// 5. Client sends one file fragment and then finishes.
// 6. Client sends multiple file fragments.

TEST_F(AsyncEndToEndTest, ServerPutDestinationIsDirectory) {
  ResetStub();
  // Accounting bits for managing CompletionQueue state.
  void* tag;
  bool cq_status;

  // The server's call to check the destination will indicate that it is a
  // directory.
  PutCallData<FakePlatform>* server_call_data =
      new PutCallData<FakePlatform>(service_.get(), server_cq_.get());

  server_call_data->platform_interface_.SetDirectoryExistsReturn(true);

  // Create components required to perform a client Put request.
  grpc::ClientContext client_ctx_;
  grpc::Status grpc_status;
  OperationStatus op_status;
  PutRequest put_request;
  PutResponse put_response;
  put_request.set_destination("/some/directory");
  put_request.clear_data();

  std::unique_ptr<grpc::ClientAsyncWriter<PutRequest>> writer_ =
      stub_->AsyncPut(&client_ctx_, &put_response, client_cq_.get(), nullptr);

  // Server should get initial client connection and begin reading.
  server_cq_->Next(&tag, &cq_status);
  server_call_data->Proceed(cq_status);

  // Wait for the request to go out, and then try to write more data to the
  // server.
  client_cq_->Next(&tag, &cq_status);
  writer_->Write(put_request, nullptr);

  // Server CompletionQueue should get the client request and reply that the
  // requested file cannot be created.
  server_cq_->Next(&tag, &cq_status);
  server_call_data->Proceed(cq_status);

  // Client will write the entire file even if the server is not listening.
  // Call Finish to collect the final status.
  // Bug: https://github.com/grpc/grpc/issues/14812
  client_cq_->Next(&tag, &cq_status);
  writer_->Finish(&grpc_status, nullptr);

  // Client gets final status from server.
  client_cq_->Next(&tag, &cq_status);
  op_status = put_response.status();
  ASSERT_EQ(op_status, OperationStatus::SERVER_CREATE_FILE_FAILURE);
  ASSERT_TRUE(grpc_status.ok());
}

TEST_F(AsyncEndToEndTest, ServerPutCreateDirectoryFailure) {
  ResetStub();
  // Accounting bits for managing CompletionQueue state.
  void* tag;
  bool cq_status;

  // The server's call to check the destination will indicate that it is a
  // directory.
  PutCallData<FakePlatform>* server_call_data =
      new PutCallData<FakePlatform>(service_.get(), server_cq_.get());

  server_call_data->platform_interface_.SetDirectoryExistsReturn(false);
  server_call_data->platform_interface_.SetCreateDirectoryReturn(false);

  // Create components required to perform a client Put request.
  grpc::ClientContext client_ctx_;
  grpc::Status grpc_status;
  OperationStatus op_status;
  PutRequest put_request;
  PutResponse put_response;
  put_request.set_destination("/privilege/issues");
  put_request.clear_data();

  std::unique_ptr<grpc::ClientAsyncWriter<PutRequest>> writer_ =
      stub_->AsyncPut(&client_ctx_, &put_response, client_cq_.get(), nullptr);

  // Server CompletionQueue will notify that there is a new client stub.
  server_cq_->Next(&tag, &cq_status);
  server_call_data->Proceed(cq_status);

  // Wait for the request to go out, and then try to write more data to the
  // server.
  client_cq_->Next(&tag, &cq_status);
  writer_->Write(put_request, nullptr);

  // Server CompletionQueue should get the client request and reply that the
  // requested file cannot be created.
  server_cq_->Next(&tag, &cq_status);
  server_call_data->Proceed(cq_status);

  // Client should call finish to get final status messages for the transfer
  // and gRPC channel.
  client_cq_->Next(&tag, &cq_status);
  writer_->Finish(&grpc_status, nullptr);

  // Client gets final status from server.
  client_cq_->Next(&tag, &cq_status);
  op_status = put_response.status();
  ASSERT_EQ(op_status, OperationStatus::SERVER_CREATE_FILE_FAILURE);
  ASSERT_TRUE(grpc_status.ok());
}

TEST_F(AsyncEndToEndTest, ServerPutCreateFileFailure) {
  ResetStub();
  // Accounting bits for managing CompletionQueue state.
  void* tag;
  bool cq_status;

  // The server's call to check the destination will indicate that it is a
  // directory.
  PutCallData<FakePlatform>* server_call_data =
      new PutCallData<FakePlatform>(service_.get(), server_cq_.get());

  server_call_data->platform_interface_.SetDirectoryExistsReturn(false);
  server_call_data->platform_interface_.SetCreateDirectoryReturn(true);
  server_call_data->platform_interface_.SetOpenFileReturn(-1);

  // Create components required to perform a client Put request.
  grpc::ClientContext client_ctx_;
  grpc::Status grpc_status;
  OperationStatus op_status;
  PutRequest put_request;
  PutResponse put_response;
  put_request.set_destination("/privilege/issues");
  put_request.clear_data();

  std::unique_ptr<grpc::ClientAsyncWriter<PutRequest>> writer_ =
      stub_->AsyncPut(&client_ctx_, &put_response, client_cq_.get(), nullptr);

  // Server CompletionQueue should notify that there is a new stub.
  server_cq_->Next(&tag, &cq_status);
  server_call_data->Proceed(cq_status);

  // Wait for the request to go out, and then try to write more data to the
  // server.
  client_cq_->Next(&tag, &cq_status);
  writer_->Write(put_request, nullptr);

  // Server CompletionQueue should get the client request and reply that the
  // requested file cannot be created.
  server_cq_->Next(&tag, &cq_status);
  server_call_data->Proceed(cq_status);

  // Client should call finish to get final status messages for the transfer
  // and gRPC channel.
  client_cq_->Next(&tag, &cq_status);
  writer_->Finish(&grpc_status, nullptr);

  // Client gets final status from server.
  client_cq_->Next(&tag, &cq_status);
  op_status = put_response.status();
  ASSERT_EQ(op_status, OperationStatus::SERVER_FILE_WRITE_FAILURE);
  ASSERT_TRUE(grpc_status.ok());
}

TEST_F(AsyncEndToEndTest, ServerPutWriteFileFailure) {
  ResetStub();
  // Accounting bits for managing CompletionQueue state.
  void* tag;
  bool cq_status;

  // The server's call to check the destination will indicate that it is a
  // directory.
  PutCallData<FakePlatform>* server_call_data =
      new PutCallData<FakePlatform>(service_.get(), server_cq_.get());

  server_call_data->platform_interface_.SetDirectoryExistsReturn(false);
  server_call_data->platform_interface_.SetCreateDirectoryReturn(true);
  server_call_data->platform_interface_.SetOpenFileReturn(1);
  server_call_data->platform_interface_.SetWriteFileReturn(-1);

  // Create components required to perform a client Put request.
  grpc::ClientContext client_ctx_;
  grpc::Status grpc_status;
  OperationStatus op_status;
  PutRequest put_request;
  PutResponse put_response;
  put_request.set_destination("/write/fail/path");
  put_request.clear_data();

  std::unique_ptr<grpc::ClientAsyncWriter<PutRequest>> writer_ =
      stub_->AsyncPut(&client_ctx_, &put_response, client_cq_.get(), nullptr);

  // Server CompletionQueue should notify that there is a new stub.
  server_cq_->Next(&tag, &cq_status);
  server_call_data->Proceed(cq_status);

  // Wait for the request to go out, and then try to write more data to the
  // server.
  client_cq_->Next(&tag, &cq_status);
  writer_->Write(put_request, nullptr);

  // Server CompletionQueue should get the client request and reply that the
  // requested file cannot be written.
  server_cq_->Next(&tag, &cq_status);
  server_call_data->Proceed(cq_status);

  // Client should get the server's message and then call finish to get final
  // status messages for the transfer and gRPC channel.
  client_cq_->Next(&tag, &cq_status);
  writer_->Finish(&grpc_status, nullptr);

  // Client gets final status from server.
  client_cq_->Next(&tag, &cq_status);
  op_status = put_response.status();
  ASSERT_EQ(op_status, OperationStatus::SERVER_FILE_WRITE_FAILURE);
  ASSERT_TRUE(grpc_status.ok());
}

TEST_F(AsyncEndToEndTest, ServerPutOneFragment) {
  ResetStub();
  // Accounting bits for managing CompletionQueue state.
  void* tag;
  bool cq_status;

  // The server's call to check the destination will indicate that it is a
  // directory.
  PutCallData<FakePlatform>* server_call_data =
      new PutCallData<FakePlatform>(service_.get(), server_cq_.get());

  server_call_data->platform_interface_.SetDirectoryExistsReturn(false);
  server_call_data->platform_interface_.SetCreateDirectoryReturn(true);
  server_call_data->platform_interface_.SetOpenFileReturn(1);
  server_call_data->platform_interface_.SetWriteFileReturn(1);
  server_call_data->platform_interface_.SetCloseFileReturn(0);

  // Create components required to perform a client Put request.
  grpc::ClientContext client_ctx_;
  grpc::Status grpc_status;
  OperationStatus op_status;
  PutRequest put_request;
  PutResponse put_response;
  put_request.set_destination("/destination/file");
  put_request.clear_data();

  std::unique_ptr<grpc::ClientAsyncWriter<PutRequest>> writer_ =
      stub_->AsyncPut(&client_ctx_, &put_response, client_cq_.get(), nullptr);

  // Server CompletionQueue should notify that there is a new stub.
  server_cq_->Next(&tag, &cq_status);
  server_call_data->Proceed(cq_status);

  // Wait for the request to go out, and then try to write a fragment to the
  // server.
  client_cq_->Next(&tag, &cq_status);
  writer_->Write(put_request, nullptr);

  // Server CompletionQueue should get the client request, write out the
  // contents, and wait for more data.
  server_cq_->Next(&tag, &cq_status);
  server_call_data->Proceed(cq_status);

  // Client should call WritesDone to indicate that the transfer is complete.
  client_cq_->Next(&tag, &cq_status);
  writer_->WritesDone(nullptr);

  // Server should get the final message from the client and report status
  // information.
  server_cq_->Next(&tag, &cq_status);
  ASSERT_FALSE(cq_status);
  server_call_data->Proceed(cq_status);

  // Client should get the server's message and then call finish to get final
  // status messages for the transfer and gRPC channel.
  client_cq_->Next(&tag, &cq_status);
  writer_->Finish(&grpc_status, nullptr);

  // Client gets final status from server.
  client_cq_->Next(&tag, &cq_status);
  op_status = put_response.status();
  ASSERT_EQ(op_status, OperationStatus::OK);
  ASSERT_TRUE(grpc_status.ok());
}

TEST_F(AsyncEndToEndTest, ServerPutMultipleFragments) {
  ResetStub();
  // Accounting bits for managing CompletionQueue state.
  void* tag;
  bool cq_status;

  // The server's call to check the destination will indicate that it is a
  // directory.
  PutCallData<FakePlatform>* server_call_data =
      new PutCallData<FakePlatform>(service_.get(), server_cq_.get());

  server_call_data->platform_interface_.SetDirectoryExistsReturn(false);
  server_call_data->platform_interface_.SetCreateDirectoryReturn(true);
  server_call_data->platform_interface_.SetOpenFileReturn(1);
  server_call_data->platform_interface_.SetWriteFileReturn(1);
  server_call_data->platform_interface_.SetCloseFileReturn(0);

  // Create components required to perform a client Put request.
  grpc::ClientContext client_ctx_;
  grpc::Status grpc_status;
  OperationStatus op_status;
  PutRequest put_request;
  PutResponse put_response;
  put_request.set_destination("/destination/file");
  put_request.clear_data();

  std::unique_ptr<grpc::ClientAsyncWriter<PutRequest>> writer_ =
      stub_->AsyncPut(&client_ctx_, &put_response, client_cq_.get(), nullptr);

  // Server CompletionQueue gets new client stub.
  server_cq_->Next(&tag, &cq_status);
  server_call_data->Proceed(cq_status);

  // Wait for the request to go out, and then try to write a fragment to the
  // server.
  client_cq_->Next(&tag, &cq_status);
  writer_->Write(put_request, nullptr);

  // Server CompletionQueue should get the client request, write out the
  // contents, and wait for more data.
  server_cq_->Next(&tag, &cq_status);
  server_call_data->Proceed(cq_status);

  // Write a second fragment to the server.
  client_cq_->Next(&tag, &cq_status);
  writer_->Write(put_request, nullptr);

  // Server processes the second fragment.
  server_cq_->Next(&tag, &cq_status);
  server_call_data->Proceed(cq_status);

  // Client should get the server's message and then call WritesDone to
  // indicate that the transfer is complete.
  client_cq_->Next(&tag, &cq_status);
  writer_->WritesDone(nullptr);

  // Server should get the final message from the client and report status
  // information.
  server_cq_->Next(&tag, &cq_status);
  server_call_data->Proceed(cq_status);

  // Client should get the server's message and then call finish to get final
  // status information.
  client_cq_->Next(&tag, &cq_status);
  writer_->Finish(&grpc_status, nullptr);

  // Client gets final status from server.
  client_cq_->Next(&tag, &cq_status);
  op_status = put_response.status();
  ASSERT_EQ(op_status, OperationStatus::OK);
  ASSERT_TRUE(grpc_status.ok());
}

// Server Exec State Machine Test Cases
// 1. Server fails to create subprocess.
// 2. Exec Read fails to write to subprocess stdin.
// 3. Exec Read finishes when client is done writing to stdin.
// 4. Exec Read finishes when child process has exited.
// 5. Exec Write sends stdin/stderr to client until subprocess exits.

TEST_F(AsyncEndToEndTest, Server_Exec_ForkFail) {
  ResetStub();
  // Accounting bits for managing CompletionQueue state.
  void* tag;
  bool cq_status;

  // The server's call to Exec should fail.
  ExecCallData<FakePlatform>* server_call_data =
      new ExecCallData<FakePlatform>(service_.get(), server_cq_.get());
  server_call_data->platform_interface_.SetExecReturn(-1);

  // Client creates a new stub.
  std::unique_ptr<grpc::ClientContext> cli_ctx = std::make_unique<grpc::ClientContext>();
  std::unique_ptr<grpc::ClientAsyncReaderWriter<ExecRequest, ExecResponse>> cli_rw;

  cli_rw = stub_->AsyncExec(cli_ctx.get(), client_cq_.get(), nullptr);

  // Queue up a read to be populated eventually when the server finishes.  Use
  // the address of the exec response so we can enforce that the read operation
  // has been performed later in the test.
  ExecResponse exec_response;
  cli_rw->Read(&exec_response, &exec_response);

  // Server should get the new stub request and issue a read.
  server_call_data->platform_interface_.SetParseCommandReturn({"echo", "hello"});
  server_cq_->Next(&tag, &cq_status);
  server_call_data->Proceed(cq_status);

  // Client should be notified that its stub has been created.
  client_cq_->Next(&tag, &cq_status);

  ExecRequest exec_request;
  exec_request.clear_argv();
  exec_request.clear_env_vars();
  exec_request.clear_std_in();
  cli_rw->Write(exec_request, nullptr);

  // Server should get the write first
  server_cq_->Next(&tag, &cq_status);
  server_call_data->Proceed(cq_status);

  // Flush the write operation completion notification.
  client_cq_->Next(&tag, &cq_status);

  // Ensure that the client queue has a new entry and that it is the response
  // to the read request.
  client_cq_->Next(&tag, &cq_status);
  ASSERT_EQ(tag, &exec_response);

  // The client calls Finish to get final status.
  grpc::Status grpc_status;
  cli_rw->Finish(&grpc_status, nullptr);

  client_cq_->Next(&tag, &cq_status);

  ASSERT_TRUE(grpc_status.ok());
  ASSERT_EQ(exec_response.status(), OperationStatus::SERVER_EXEC_FORK_FAILURE);
}

TEST_F(AsyncEndToEndTest, Server_ExecRead_StdinEof) {
  ResetStub();
  // Accounting bits for managing CompletionQueue state.
  void* tag;
  bool cq_status;

  // Exec service boilerplate.
  std::shared_ptr<grpc::ServerContext> srv_ctx = std::make_shared<grpc::ServerContext>();
  std::shared_ptr<grpc::ServerAsyncReaderWriter<ExecResponse, ExecRequest>> srv_rw =
      std::make_shared<grpc::ServerAsyncReaderWriter<ExecResponse, ExecRequest>>(srv_ctx.get());

  service_->RequestExec(srv_ctx.get(), srv_rw.get(), server_cq_.get(), server_cq_.get(), this);

  // Client creates a new stub.
  std::unique_ptr<grpc::ClientContext> cli_ctx = std::make_unique<grpc::ClientContext>();
  std::unique_ptr<grpc::ClientAsyncReaderWriter<ExecRequest, ExecResponse>> cli_rw;

  cli_rw = stub_->AsyncExec(cli_ctx.get(), client_cq_.get(), nullptr);

  // The read side of the server exec routine should see that the subprocess
  // is still alive, but should fail to write to its stdin and then delete
  // itself.
  server_cq_->Next(&tag, &cq_status);

  ExecReadCallData<FakePlatform>* server_call_data =
      new ExecReadCallData<FakePlatform>(srv_ctx, srv_rw, 0, 0);
  server_call_data->platform_interface_.SetKillPidReturn(0);
  server_call_data->platform_interface_.SetWriteFileReturn(-1);

  // Client sends something to subprocess stdin.
  client_cq_->Next(&tag, &cq_status);

  ExecRequest exec_request;
  exec_request.clear_argv();
  exec_request.clear_env_vars();
  exec_request.clear_std_in();

  cli_rw->Write(exec_request, nullptr);
  client_cq_->Next(&tag, &cq_status);

  // The server will get the request, attempt to write to the subprocess stdin,
  // fail, and delete itself.  The only indication of the failure will be that
  // the reference count to the ServerAsyncReaderWriter decrements.
  uint32_t initial_use_count = srv_rw.use_count();
  server_cq_->Next(&tag, &cq_status);
  server_call_data->Proceed(cq_status);
  uint32_t final_use_count = srv_rw.use_count();

  ASSERT_LT(final_use_count, initial_use_count);
}

TEST_F(AsyncEndToEndTest, Server_ExecRead_ClientDone) {
  ResetStub();
  // Accounting bits for managing CompletionQueue state.
  void* tag;
  bool cq_status;

  // Exec service boilerplate.
  std::shared_ptr<grpc::ServerContext> srv_ctx = std::make_shared<grpc::ServerContext>();
  std::shared_ptr<grpc::ServerAsyncReaderWriter<ExecResponse, ExecRequest>> srv_rw =
      std::make_shared<grpc::ServerAsyncReaderWriter<ExecResponse, ExecRequest>>(srv_ctx.get());

  service_->RequestExec(srv_ctx.get(), srv_rw.get(), server_cq_.get(), server_cq_.get(), this);

  // Client creates a new stub.
  std::unique_ptr<grpc::ClientContext> cli_ctx = std::make_unique<grpc::ClientContext>();
  std::unique_ptr<grpc::ClientAsyncReaderWriter<ExecRequest, ExecResponse>> cli_rw;

  cli_rw = stub_->AsyncExec(cli_ctx.get(), client_cq_.get(), nullptr);

  // The read side of the server exec routine should see that the subprocess
  // is still alive and succeed in writing to it.
  server_cq_->Next(&tag, &cq_status);

  ExecReadCallData<FakePlatform>* server_call_data =
      new ExecReadCallData<FakePlatform>(srv_ctx, srv_rw, 0, 0);
  server_call_data->platform_interface_.SetKillPidReturn(0);
  server_call_data->platform_interface_.SetWriteFileReturn(1);

  // Client sends something to subprocess stdin.
  client_cq_->Next(&tag, &cq_status);

  ExecRequest exec_request;
  exec_request.clear_argv();
  exec_request.clear_env_vars();
  exec_request.clear_std_in();

  cli_rw->Write(exec_request, nullptr);

  // Server writes it into the subprocess.
  server_cq_->Next(&tag, &cq_status);
  server_call_data->Proceed(cq_status);

  // Client indicates that it is done writing.
  client_cq_->Next(&tag, &cq_status);
  cli_rw->WritesDone(nullptr);

  // The server will get a false status from the completion queue and delete
  // itself.  The only indication of the failure will be that the reference
  // count to the ServerAsyncReaderWriter decrements.
  uint32_t initial_use_count = srv_rw.use_count();

  server_cq_->Next(&tag, &cq_status);
  ASSERT_FALSE(cq_status);
  server_call_data->Proceed(cq_status);

  uint32_t final_use_count = srv_rw.use_count();

  ASSERT_LT(final_use_count, initial_use_count);
}

TEST_F(AsyncEndToEndTest, Server_ExecRead_SubprocessExits) {
  ResetStub();
  // Accounting bits for managing CompletionQueue state.
  void* tag;
  bool cq_status;

  // Exec service boilerplate.
  std::shared_ptr<grpc::ServerContext> srv_ctx = std::make_shared<grpc::ServerContext>();
  std::shared_ptr<grpc::ServerAsyncReaderWriter<ExecResponse, ExecRequest>> srv_rw =
      std::make_shared<grpc::ServerAsyncReaderWriter<ExecResponse, ExecRequest>>(srv_ctx.get());

  service_->RequestExec(srv_ctx.get(), srv_rw.get(), server_cq_.get(), server_cq_.get(), this);

  // Client creates a new stub.
  std::unique_ptr<grpc::ClientContext> cli_ctx = std::make_unique<grpc::ClientContext>();
  std::unique_ptr<grpc::ClientAsyncReaderWriter<ExecRequest, ExecResponse>> cli_rw;

  cli_rw = stub_->AsyncExec(cli_ctx.get(), client_cq_.get(), nullptr);

  // The read side of the server exec routine should see that the subprocess
  // has exited.
  server_cq_->Next(&tag, &cq_status);

  ExecReadCallData<FakePlatform>* server_call_data =
      new ExecReadCallData<FakePlatform>(srv_ctx, srv_rw, 0, 0);
  server_call_data->platform_interface_.SetKillPidReturn(-1);

  // Client sends something to subprocess stdin.
  client_cq_->Next(&tag, &cq_status);

  ExecRequest exec_request;
  exec_request.clear_argv();
  exec_request.clear_env_vars();
  exec_request.clear_std_in();

  cli_rw->Write(exec_request, nullptr);

  // The server will get the request, realize the subprocess has exited, and
  // delete itself.  The only indication of the failure will be that the
  // reference count to the ServerAsyncReaderWriter decrements.
  uint32_t initial_use_count = srv_rw.use_count();
  server_cq_->Next(&tag, &cq_status);
  server_call_data->Proceed(cq_status);
  uint32_t final_use_count = srv_rw.use_count();

  ASSERT_LT(final_use_count, initial_use_count);

  // Issue a finish on behalf of the server.
  srv_rw->Finish(grpc::Status::OK, nullptr);
  server_cq_->Next(&tag, &cq_status);

  // Cleanup the client reader-writer.
  client_cq_->Next(&tag, &cq_status);
  cli_rw->WritesDone(nullptr);

  client_cq_->Next(&tag, &cq_status);
  grpc::Status grpc_status;
  cli_rw->Finish(&grpc_status, nullptr);

  client_cq_->Next(&tag, &cq_status);
  ASSERT_TRUE(grpc_status.ok());
}

TEST_F(AsyncEndToEndTest, Server_ExecWrite_WriteUntilChildExits) {
  ResetStub();
  // Accounting bits for managing CompletionQueue state.
  void* tag;
  bool cq_status;

  // Exec service boilerplate.
  std::shared_ptr<grpc::ServerContext> srv_ctx = std::make_shared<grpc::ServerContext>();
  std::shared_ptr<grpc::ServerAsyncReaderWriter<ExecResponse, ExecRequest>> srv_rw =
      std::make_shared<grpc::ServerAsyncReaderWriter<ExecResponse, ExecRequest>>(srv_ctx.get());

  service_->RequestExec(srv_ctx.get(), srv_rw.get(), server_cq_.get(), server_cq_.get(), this);

  // Client creates a new stub.
  std::unique_ptr<grpc::ClientContext> cli_ctx = std::make_unique<grpc::ClientContext>();
  std::unique_ptr<grpc::ClientAsyncReaderWriter<ExecRequest, ExecResponse>> cli_rw;

  cli_rw = stub_->AsyncExec(cli_ctx.get(), client_cq_.get(), nullptr);

  // The write side of the server exec routine will poll the child pid and see
  // that it has exited.
  server_cq_->Next(&tag, &cq_status);

  ExecWriteCallData<FakePlatform>* server_call_data =
      new ExecWriteCallData<FakePlatform>(srv_ctx, srv_rw, 0, 0, 0);

  // Client reads from the server.
  client_cq_->Next(&tag, &cq_status);

  ExecResponse exec_response;
  cli_rw->Read(&exec_response, nullptr);

  // The server will finish and delete itself.
  server_cq_->Next(&tag, &cq_status);
  server_call_data->Proceed(cq_status);

  // The client will get the initial server write and issue another read.
  client_cq_->Next(&tag, &cq_status);
  cli_rw->Read(&exec_response, nullptr);

  // The client should see that the server has finished and request the finish
  // status.
  client_cq_->Next(&tag, &cq_status);
  ASSERT_FALSE(cq_status);

  grpc::Status grpc_status;

  cli_rw->Finish(&grpc_status, nullptr);
  client_cq_->Next(&tag, &cq_status);

  ASSERT_TRUE(grpc_status.ok());
}
