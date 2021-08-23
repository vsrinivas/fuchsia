// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>

#include <grpc/support/log.h>
#include <gtest/gtest.h>

#include "src/virtualization/lib/guest_interaction/server/server_operation_state.h"
#include "src/virtualization/lib/guest_interaction/test/operation_test_lib.h"

#include <grpc++/grpc++.h>

// Server Get State Machine Test Cases
//
// 1. Requested file does not exist.
// 2. Server fails to open the requested file.
// 3. Requested file is below the fragmentation size.
// 4. Requested file is above the fragmentation size.
// 5. The file the server is reading from goes into a bad state.

TEST_F(AsyncEndToEndTest, ServerMissingFile) {
  // The server's call to check if the requested file exists will return false.
  GetCallData<FakePlatform>* server_call_data =
      new GetCallData<FakePlatform>(service_.get(), server_cq_.get());

  server_call_data->platform_interface_.SetFileExistsReturn(false);

  // Create components required to perform a client Get request.
  grpc::ClientContext client_ctx;
  std::unique_ptr<grpc::ClientAsyncReader<GetResponse>> reader = stub_->AsyncGet(
      &client_ctx,
      []() {
        GetRequest get_request;
        get_request.set_source("/some/bogus/path");
        return get_request;
      }(),
      client_cq_.get(), &client_ctx);

  // Wait for the request to go out, and then request to read from the server.
  ASSERT_GRPC_CQ_NEXT(client_cq_, &client_ctx, true);
  GetResponse get_response;
  reader->Read(&get_response, reader.get());

  // Server CompletionQueue should get the client request and reply that the
  // requested file does not exist.
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  // Client should get the server's message and then wait for the server to
  // call Finish.
  ASSERT_GRPC_CQ_NEXT(client_cq_, reader.get(), true);
  ASSERT_EQ(get_response.status(), OperationStatus::SERVER_MISSING_FILE_FAILURE);
  ASSERT_EQ(get_response.data(), "");
  grpc::Status grpc_status;
  reader->Finish(&grpc_status, reader.get());

  // Server finishes.
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  // Client gets final status from server.
  ASSERT_GRPC_CQ_NEXT(client_cq_, reader.get(), true);
  ASSERT_TRUE(grpc_status.ok()) << grpc_status.error_message();
}

TEST_F(AsyncEndToEndTest, ServerFileOpenFailure) {
  // The server's call to check if the requested file exists will return false.
  GetCallData<FakePlatform>* server_call_data =
      new GetCallData<FakePlatform>(service_.get(), server_cq_.get());

  server_call_data->platform_interface_.SetFileExistsReturn(true);
  server_call_data->platform_interface_.SetOpenFileReturn(-1);

  // Create components required to perform a client Get request.
  grpc::ClientContext client_ctx;
  std::unique_ptr<grpc::ClientAsyncReader<GetResponse>> reader = stub_->AsyncGet(
      &client_ctx,
      []() {
        GetRequest get_request;
        get_request.set_source("/file/with/permissions/issues");
        return get_request;
      }(),
      client_cq_.get(), &client_ctx);

  // Wait for the request to go out, and then request to read from the server.
  ASSERT_GRPC_CQ_NEXT(client_cq_, &client_ctx, true);
  GetResponse get_response;
  reader->Read(&get_response, reader.get());

  // Server CompletionQueue should get the client request and reply that the
  // requested file does not exist.
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  // Client should get the server's message and then wait for the server to
  // call Finish.
  ASSERT_GRPC_CQ_NEXT(client_cq_, reader.get(), true);
  ASSERT_EQ(get_response.status(), OperationStatus::SERVER_FILE_READ_FAILURE);
  ASSERT_EQ(get_response.data(), "");
  grpc::Status grpc_status;
  reader->Finish(&grpc_status, reader.get());

  // Server finishes.
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  // Client gets final status from server.
  ASSERT_GRPC_CQ_NEXT(client_cq_, reader.get(), true);
  ASSERT_TRUE(grpc_status.ok()) << grpc_status.error_message();
}

TEST_F(AsyncEndToEndTest, ServerUnfragmentedRead) {
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
  grpc::ClientContext client_ctx;
  std::unique_ptr<grpc::ClientAsyncReader<GetResponse>> reader = stub_->AsyncGet(
      &client_ctx,
      []() {
        GetRequest get_request;
        get_request.set_source("/some/test/file");
        return get_request;
      }(),
      client_cq_.get(), &client_ctx);

  // Wait for the request to go out, and then request to read from the server.
  ASSERT_GRPC_CQ_NEXT(client_cq_, &client_ctx, true);
  GetResponse get_response;
  reader->Read(&get_response, reader.get());

  // Server CompletionQueue should get the client request, open the file, and
  // send back the first chunk of contents.
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  // Client should get the server's message and then wait for more data.
  ASSERT_GRPC_CQ_NEXT(client_cq_, reader.get(), true);
  ASSERT_EQ(get_response.status(), OperationStatus::OK);
  ASSERT_EQ(get_response.data(), "test");
  reader->Read(&get_response, reader.get());

  // The server should hit EOF and send an empty chunk of data back to the
  // client.
  server_call_data->platform_interface_.SetReadFileContents("");
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  ASSERT_GRPC_CQ_NEXT(client_cq_, reader.get(), true);
  ASSERT_EQ(get_response.status(), OperationStatus::OK);
  ASSERT_EQ(get_response.data(), "");
  reader->Read(&get_response, reader.get());

  // The server should then call Finish.
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  // The client read should fail and then it should finish.
  ASSERT_GRPC_CQ_NEXT(client_cq_, reader.get(), false);
  grpc::Status grpc_status;
  reader->Finish(&grpc_status, reader.get());

  // Client gets final status from server.
  ASSERT_GRPC_CQ_NEXT(client_cq_, reader.get(), true);
  ASSERT_TRUE(grpc_status.ok()) << grpc_status.error_message();
}

TEST_F(AsyncEndToEndTest, ServerFragmentedRead) {
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
  grpc::ClientContext client_ctx;
  std::unique_ptr<grpc::ClientAsyncReader<GetResponse>> reader = stub_->AsyncGet(
      &client_ctx,
      []() {
        GetRequest get_request;
        get_request.set_source("/some/test/file");
        return get_request;
      }(),
      client_cq_.get(), &client_ctx);

  // Wait for the request to go out, and then request to read from the server.
  ASSERT_GRPC_CQ_NEXT(client_cq_, &client_ctx, true);
  GetResponse get_response;
  reader->Read(&get_response, reader.get());

  // Server CompletionQueue should get the client request, open the file, and
  // send back the first chunk of contents.
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  // Client should get the server's message and then wait for more data.
  ASSERT_GRPC_CQ_NEXT(client_cq_, reader.get(), true);
  ASSERT_EQ(get_response.status(), OperationStatus::OK);
  ASSERT_EQ(get_response.data(), "test");
  reader->Read(&get_response, reader.get());

  // Repeat the process.
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  ASSERT_GRPC_CQ_NEXT(client_cq_, reader.get(), true);
  ASSERT_EQ(get_response.status(), OperationStatus::OK);
  ASSERT_EQ(get_response.data(), "test");
  reader->Read(&get_response, reader.get());

  // The server should hit EOF and send an empty chunk of data back to the
  // client.
  server_call_data->platform_interface_.SetReadFileContents("");
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  ASSERT_GRPC_CQ_NEXT(client_cq_, reader.get(), true);
  ASSERT_EQ(get_response.status(), OperationStatus::OK);
  ASSERT_EQ(get_response.data(), "");
  reader->Read(&get_response, reader.get());

  // The server should then call Finish.
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  // The client read should fail and then it should finish.
  ASSERT_GRPC_CQ_NEXT(client_cq_, reader.get(), false);
  grpc::Status grpc_status;
  reader->Finish(&grpc_status, reader.get());

  // Client gets final status from server.
  ASSERT_GRPC_CQ_NEXT(client_cq_, reader.get(), true);
  ASSERT_TRUE(grpc_status.ok()) << grpc_status.error_message();
}

TEST_F(AsyncEndToEndTest, ServerBadFile) {
  // The server will be told that the file exists and that it was able to open
  // open it.  When the server goes to read from the file, it will be informed
  // that the file is bad.
  GetCallData<FakePlatform>* server_call_data =
      new GetCallData<FakePlatform>(service_.get(), server_cq_.get());

  server_call_data->platform_interface_.SetFileExistsReturn(true);
  server_call_data->platform_interface_.SetOpenFileReturn(1);
  server_call_data->platform_interface_.SetReadFileReturn(-1);

  // Create components required to perform a client Get request.
  grpc::ClientContext client_ctx;
  std::unique_ptr<grpc::ClientAsyncReader<GetResponse>> reader = stub_->AsyncGet(
      &client_ctx,
      []() {
        GetRequest get_request;
        get_request.set_source("/some/test/file");
        return get_request;
      }(),
      client_cq_.get(), &client_ctx);

  // Wait for the request to go out and then request to read from the server.
  ASSERT_GRPC_CQ_NEXT(client_cq_, &client_ctx, true);
  GetResponse get_response;
  reader->Read(&get_response, reader.get());

  // Server CompletionQueue should get the client request, open the file, and
  // send back a response indicating the read failed.
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  // Client should get the server's message and then wait for more data.
  ASSERT_GRPC_CQ_NEXT(client_cq_, reader.get(), true);
  ASSERT_EQ(get_response.status(), OperationStatus::SERVER_FILE_READ_FAILURE);
  ASSERT_EQ(get_response.data(), "");
  reader->Read(&get_response, reader.get());

  // The server should finish.
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  // The client will get back a bad status and finish.
  ASSERT_GRPC_CQ_NEXT(client_cq_, reader.get(), false);
  grpc::Status grpc_status;
  reader->Finish(&grpc_status, reader.get());

  // Client gets final status from server.
  ASSERT_GRPC_CQ_NEXT(client_cq_, reader.get(), true);
  ASSERT_TRUE(grpc_status.ok()) << grpc_status.error_message();
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
  // The server's call to check the destination will indicate that it is a
  // directory.
  PutCallData<FakePlatform>* server_call_data =
      new PutCallData<FakePlatform>(service_.get(), server_cq_.get());

  server_call_data->platform_interface_.SetDirectoryExistsReturn(true);

  // Create components required to perform a client Put request.
  grpc::ClientContext client_ctx;
  grpc::Status grpc_status;
  OperationStatus op_status;
  PutRequest put_request;
  PutResponse put_response;
  put_request.set_destination("/some/directory");
  put_request.clear_data();

  std::unique_ptr<grpc::ClientAsyncWriter<PutRequest>> writer =
      stub_->AsyncPut(&client_ctx, &put_response, client_cq_.get(), &client_ctx);

  // Server should get initial client connection and begin reading.
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  // Wait for the request to go out, and then try to write more data to the
  // server.
  ASSERT_GRPC_CQ_NEXT(client_cq_, &client_ctx, true);
  writer->Write(put_request, writer.get());

  // Server CompletionQueue should get the client request and reply that the
  // requested file cannot be created.
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  // Client will write the entire file even if the server is not listening.
  // Call Finish to collect the final status.
  // Bug: https://github.com/grpc/grpc/issues/14812
  ASSERT_GRPC_CQ_NEXT(client_cq_, writer.get(), true);
  writer->Finish(&grpc_status, writer.get());

  // Client gets final status from server.
  ASSERT_GRPC_CQ_NEXT(client_cq_, writer.get(), true);
  op_status = put_response.status();

  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  ASSERT_EQ(op_status, OperationStatus::SERVER_CREATE_FILE_FAILURE);
  ASSERT_TRUE(grpc_status.ok()) << grpc_status.error_message();
}

TEST_F(AsyncEndToEndTest, ServerPutCreateDirectoryFailure) {
  // The server's call to check the destination will indicate that it is a
  // directory.
  PutCallData<FakePlatform>* server_call_data =
      new PutCallData<FakePlatform>(service_.get(), server_cq_.get());

  server_call_data->platform_interface_.SetDirectoryExistsReturn(false);
  server_call_data->platform_interface_.SetCreateDirectoryReturn(false);

  // Create components required to perform a client Put request.
  grpc::ClientContext client_ctx;
  grpc::Status grpc_status;
  OperationStatus op_status;
  PutRequest put_request;
  PutResponse put_response;
  put_request.set_destination("/privilege/issues");
  put_request.clear_data();

  std::unique_ptr<grpc::ClientAsyncWriter<PutRequest>> writer =
      stub_->AsyncPut(&client_ctx, &put_response, client_cq_.get(), &client_ctx);

  // Server CompletionQueue will notify that there is a new client stub.
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  // Wait for the request to go out, and then try to write more data to the
  // server.
  ASSERT_GRPC_CQ_NEXT(client_cq_, &client_ctx, true);
  writer->Write(put_request, writer.get());

  // Server CompletionQueue should get the client request and reply that the
  // requested file cannot be created.
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  // Client should call finish to get final status messages for the transfer
  // and gRPC channel.
  ASSERT_GRPC_CQ_NEXT(client_cq_, writer.get(), true);
  writer->Finish(&grpc_status, writer.get());

  // Client gets final status from server.
  ASSERT_GRPC_CQ_NEXT(client_cq_, writer.get(), true);
  op_status = put_response.status();

  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  ASSERT_EQ(op_status, OperationStatus::SERVER_CREATE_FILE_FAILURE);
  ASSERT_TRUE(grpc_status.ok()) << grpc_status.error_message();
}

TEST_F(AsyncEndToEndTest, ServerPutCreateFileFailure) {
  // The server's call to check the destination will indicate that it is a
  // directory.
  PutCallData<FakePlatform>* server_call_data =
      new PutCallData<FakePlatform>(service_.get(), server_cq_.get());

  server_call_data->platform_interface_.SetDirectoryExistsReturn(false);
  server_call_data->platform_interface_.SetCreateDirectoryReturn(true);
  server_call_data->platform_interface_.SetOpenFileReturn(-1);

  // Create components required to perform a client Put request.
  grpc::ClientContext client_ctx;
  grpc::Status grpc_status;
  OperationStatus op_status;
  PutRequest put_request;
  PutResponse put_response;
  put_request.set_destination("/privilege/issues");
  put_request.clear_data();

  std::unique_ptr<grpc::ClientAsyncWriter<PutRequest>> writer =
      stub_->AsyncPut(&client_ctx, &put_response, client_cq_.get(), &client_ctx);

  // Server CompletionQueue should notify that there is a new stub.
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  // Wait for the request to go out, and then try to write more data to the
  // server.
  ASSERT_GRPC_CQ_NEXT(client_cq_, &client_ctx, true);
  writer->Write(put_request, writer.get());

  // Server CompletionQueue should get the client request and reply that the
  // requested file cannot be created.
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  // Client should call finish to get final status messages for the transfer
  // and gRPC channel.
  ASSERT_GRPC_CQ_NEXT(client_cq_, writer.get(), true);
  writer->Finish(&grpc_status, writer.get());

  // Client gets final status from server.
  ASSERT_GRPC_CQ_NEXT(client_cq_, writer.get(), true);
  op_status = put_response.status();

  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  ASSERT_EQ(op_status, OperationStatus::SERVER_FILE_WRITE_FAILURE);
  ASSERT_TRUE(grpc_status.ok()) << grpc_status.error_message();
}

TEST_F(AsyncEndToEndTest, ServerPutWriteFileFailure) {
  // The server's call to check the destination will indicate that it is a
  // directory.
  PutCallData<FakePlatform>* server_call_data =
      new PutCallData<FakePlatform>(service_.get(), server_cq_.get());

  server_call_data->platform_interface_.SetDirectoryExistsReturn(false);
  server_call_data->platform_interface_.SetCreateDirectoryReturn(true);
  server_call_data->platform_interface_.SetOpenFileReturn(1);
  server_call_data->platform_interface_.SetWriteFileReturn(-1);

  // Create components required to perform a client Put request.
  grpc::ClientContext client_ctx;
  grpc::Status grpc_status;
  OperationStatus op_status;
  PutRequest put_request;
  PutResponse put_response;
  put_request.set_destination("/write/fail/path");
  put_request.clear_data();

  std::unique_ptr<grpc::ClientAsyncWriter<PutRequest>> writer =
      stub_->AsyncPut(&client_ctx, &put_response, client_cq_.get(), &client_ctx);

  // Server CompletionQueue should notify that there is a new stub.
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  // Wait for the request to go out, and then try to write more data to the
  // server.
  ASSERT_GRPC_CQ_NEXT(client_cq_, &client_ctx, true);
  writer->Write(put_request, writer.get());

  // Server CompletionQueue should get the client request and reply that the
  // requested file cannot be written.
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  // Client should get the server's message and then call finish to get final
  // status messages for the transfer and gRPC channel.
  ASSERT_GRPC_CQ_NEXT(client_cq_, writer.get(), true);
  writer->Finish(&grpc_status, writer.get());

  // Client gets final status from server.
  ASSERT_GRPC_CQ_NEXT(client_cq_, writer.get(), true);
  op_status = put_response.status();

  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  ASSERT_EQ(op_status, OperationStatus::SERVER_FILE_WRITE_FAILURE);
  ASSERT_TRUE(grpc_status.ok()) << grpc_status.error_message();
}

TEST_F(AsyncEndToEndTest, ServerPutOneFragment) {
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
  grpc::ClientContext client_ctx;
  grpc::Status grpc_status;
  OperationStatus op_status;
  PutRequest put_request;
  PutResponse put_response;
  put_request.set_destination("/destination/file");
  put_request.clear_data();

  std::unique_ptr<grpc::ClientAsyncWriter<PutRequest>> writer =
      stub_->AsyncPut(&client_ctx, &put_response, client_cq_.get(), &client_ctx);

  // Server CompletionQueue should notify that there is a new stub.
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  // Wait for the request to go out, and then try to write a fragment to the
  // server.
  ASSERT_GRPC_CQ_NEXT(client_cq_, &client_ctx, true);
  writer->Write(put_request, writer.get());

  // Server CompletionQueue should get the client request, write out the
  // contents, and wait for more data.
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  // Client should call WritesDone to indicate that the transfer is complete.
  ASSERT_GRPC_CQ_NEXT(client_cq_, writer.get(), true);
  writer->WritesDone(writer.get());

  // Server should get the final message from the client and report status
  // information.
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, false);
  server_call_data->Proceed(false);

  // Client should get the server's message and then call finish to get final
  // status messages for the transfer and gRPC channel.
  ASSERT_GRPC_CQ_NEXT(client_cq_, writer.get(), true);
  writer->Finish(&grpc_status, writer.get());

  // Client gets final status from server.
  ASSERT_GRPC_CQ_NEXT(client_cq_, writer.get(), true);
  op_status = put_response.status();

  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  ASSERT_EQ(op_status, OperationStatus::OK);
  ASSERT_TRUE(grpc_status.ok()) << grpc_status.error_message();
}

TEST_F(AsyncEndToEndTest, ServerPutMultipleFragments) {
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
  grpc::ClientContext client_ctx;
  grpc::Status grpc_status;
  OperationStatus op_status;
  PutRequest put_request;
  PutResponse put_response;
  put_request.set_destination("/destination/file");
  put_request.clear_data();

  std::unique_ptr<grpc::ClientAsyncWriter<PutRequest>> writer =
      stub_->AsyncPut(&client_ctx, &put_response, client_cq_.get(), &client_ctx);

  // Server CompletionQueue gets new client stub.
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  // Wait for the request to go out, and then try to write a fragment to the
  // server.
  ASSERT_GRPC_CQ_NEXT(client_cq_, &client_ctx, true);
  writer->Write(put_request, writer.get());

  // Server CompletionQueue should get the client request, write out the
  // contents, and wait for more data.
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  // Write a second fragment to the server.
  ASSERT_GRPC_CQ_NEXT(client_cq_, writer.get(), true);
  writer->Write(put_request, writer.get());

  // Server processes the second fragment.
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  // Client should get the server's message and then call WritesDone to
  // indicate that the transfer is complete.
  ASSERT_GRPC_CQ_NEXT(client_cq_, writer.get(), true);
  writer->WritesDone(writer.get());

  // Server should get the final message from the client and report status
  // information.
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, false);
  server_call_data->Proceed(false);

  // Client should get the server's message and then call finish to get final
  // status information.
  ASSERT_GRPC_CQ_NEXT(client_cq_, writer.get(), true);
  writer->Finish(&grpc_status, writer.get());

  // Client gets final status from server.
  ASSERT_GRPC_CQ_NEXT(client_cq_, writer.get(), true);
  op_status = put_response.status();

  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  ASSERT_EQ(op_status, OperationStatus::OK);
  ASSERT_TRUE(grpc_status.ok()) << grpc_status.error_message();
}

// Server Exec State Machine Test Cases
// 1. Server fails to create subprocess.
// 2. Exec Read fails to write to subprocess stdin.
// 3. Exec Read finishes when client is done writing to stdin.
// 4. Exec Read finishes when child process has exited.
// 5. Exec Write sends stdin/stderr to client until subprocess exits.

TEST_F(AsyncEndToEndTest, Server_Exec_ForkFail) {
  // The server's call to Exec should fail.
  ExecCallData<FakePlatform>* server_call_data =
      new ExecCallData<FakePlatform>(service_.get(), server_cq_.get());
  server_call_data->platform_interface_.SetExecReturn(-1);

  // Client creates a new stub.
  std::unique_ptr<grpc::ClientContext> cli_ctx = std::make_unique<grpc::ClientContext>();
  std::unique_ptr<grpc::ClientAsyncReaderWriter<ExecRequest, ExecResponse>> cli_rw;

  cli_rw = stub_->AsyncExec(cli_ctx.get(), client_cq_.get(), cli_ctx.get());

  // Queue up a read to be populated eventually when the server finishes.  Use
  // the address of the exec response so we can enforce that the read operation
  // has been performed later in the test.
  ExecResponse exec_response;
  cli_rw->Read(&exec_response, &exec_response);

  // Server should get the new stub request and issue a read.
  server_call_data->platform_interface_.SetParseCommandReturn({"echo", "hello"});
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  // Client should be notified that its stub has been created.
  ASSERT_GRPC_CQ_NEXT(client_cq_, cli_ctx.get(), true);

  ExecRequest exec_request;
  exec_request.clear_argv();
  exec_request.clear_env_vars();
  exec_request.clear_std_in();
  cli_rw->Write(exec_request, &exec_request);

  // Server should get the write first
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  // Flush the write operation completion notification.
  ASSERT_GRPC_CQ_NEXT(client_cq_, &exec_request, true);

  // Ensure that the client queue has a new entry and that it is the response
  // to the read request.
  ASSERT_GRPC_CQ_NEXT(client_cq_, &exec_response, true);

  // The client calls Finish to get final status.
  grpc::Status grpc_status;
  cli_rw->Finish(&grpc_status, cli_rw.get());

  ASSERT_GRPC_CQ_NEXT(client_cq_, cli_rw.get(), true);

  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  ASSERT_TRUE(grpc_status.ok()) << grpc_status.error_message();
  ASSERT_EQ(exec_response.status(), OperationStatus::SERVER_EXEC_FORK_FAILURE);
}

TEST_F(AsyncEndToEndTest, Server_ExecRead_StdinEof) {
  // Exec service boilerplate.
  std::shared_ptr<grpc::ServerContext> srv_ctx = std::make_shared<grpc::ServerContext>();
  std::shared_ptr<grpc::ServerAsyncReaderWriter<ExecResponse, ExecRequest>> srv_rw =
      std::make_shared<grpc::ServerAsyncReaderWriter<ExecResponse, ExecRequest>>(srv_ctx.get());

  service_->RequestExec(srv_ctx.get(), srv_rw.get(), server_cq_.get(), server_cq_.get(),
                        srv_ctx.get());

  // Client creates a new stub.
  std::unique_ptr<grpc::ClientContext> cli_ctx = std::make_unique<grpc::ClientContext>();
  std::unique_ptr<grpc::ClientAsyncReaderWriter<ExecRequest, ExecResponse>> cli_rw;

  cli_rw = stub_->AsyncExec(cli_ctx.get(), client_cq_.get(), cli_ctx.get());

  // The read side of the server exec routine should see that the subprocess
  // is still alive, but should fail to write to its stdin and then delete
  // itself.
  ASSERT_GRPC_CQ_NEXT(server_cq_, srv_ctx.get(), true);

  ExecReadCallData<FakePlatform>* server_call_data =
      new ExecReadCallData<FakePlatform>(srv_ctx, srv_rw, 0, 0);
  server_call_data->platform_interface_.SetKillPidReturn(0);
  server_call_data->platform_interface_.SetWriteFileReturn(-1);

  // Client sends something to subprocess stdin.
  ASSERT_GRPC_CQ_NEXT(client_cq_, cli_ctx.get(), true);

  ExecRequest exec_request;
  exec_request.clear_argv();
  exec_request.clear_env_vars();
  exec_request.clear_std_in();

  cli_rw->Write(exec_request, &exec_request);
  ASSERT_GRPC_CQ_NEXT(client_cq_, &exec_request, true);

  // The server will get the request, attempt to write to the subprocess stdin,
  // fail, and delete itself.  The only indication of the failure will be that
  // the reference count to the ServerAsyncReaderWriter decrements.
  size_t initial_use_count = srv_rw.use_count();
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);
  size_t final_use_count = srv_rw.use_count();

  ASSERT_LT(final_use_count, initial_use_count);
}

TEST_F(AsyncEndToEndTest, Server_ExecRead_ClientDone) {
  // Exec service boilerplate.
  std::shared_ptr<grpc::ServerContext> srv_ctx = std::make_shared<grpc::ServerContext>();
  std::shared_ptr<grpc::ServerAsyncReaderWriter<ExecResponse, ExecRequest>> srv_rw =
      std::make_shared<grpc::ServerAsyncReaderWriter<ExecResponse, ExecRequest>>(srv_ctx.get());

  service_->RequestExec(srv_ctx.get(), srv_rw.get(), server_cq_.get(), server_cq_.get(),
                        srv_ctx.get());

  // Client creates a new stub.
  std::unique_ptr<grpc::ClientContext> cli_ctx = std::make_unique<grpc::ClientContext>();
  std::unique_ptr<grpc::ClientAsyncReaderWriter<ExecRequest, ExecResponse>> cli_rw;

  cli_rw = stub_->AsyncExec(cli_ctx.get(), client_cq_.get(), cli_ctx.get());

  // The read side of the server exec routine should see that the subprocess
  // is still alive and succeed in writing to it.
  ASSERT_GRPC_CQ_NEXT(server_cq_, srv_ctx.get(), true);

  ExecReadCallData<FakePlatform>* server_call_data =
      new ExecReadCallData<FakePlatform>(srv_ctx, srv_rw, 0, 0);
  server_call_data->platform_interface_.SetKillPidReturn(0);
  server_call_data->platform_interface_.SetWriteFileReturn(1);

  // Client sends something to subprocess stdin.
  ASSERT_GRPC_CQ_NEXT(client_cq_, cli_ctx.get(), true);

  ExecRequest exec_request;
  exec_request.clear_argv();
  exec_request.clear_env_vars();
  exec_request.clear_std_in();

  cli_rw->Write(exec_request, &exec_request);

  // Server writes it into the subprocess.
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  // Client indicates that it is done writing.
  ASSERT_GRPC_CQ_NEXT(client_cq_, &exec_request, true);
  cli_rw->WritesDone(&exec_request);

  // The server will get a false status from the completion queue and delete
  // itself.  The only indication of the failure will be that the reference
  // count to the ServerAsyncReaderWriter decrements.
  size_t initial_use_count = srv_rw.use_count();

  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, false);
  server_call_data->Proceed(false);

  ASSERT_GRPC_CQ_NEXT(client_cq_, &exec_request, true);

  size_t final_use_count = srv_rw.use_count();

  ASSERT_LT(final_use_count, initial_use_count);
}

TEST_F(AsyncEndToEndTest, Server_ExecRead_SubprocessExits) {
  // Exec service boilerplate.
  std::shared_ptr<grpc::ServerContext> srv_ctx = std::make_shared<grpc::ServerContext>();
  std::shared_ptr<grpc::ServerAsyncReaderWriter<ExecResponse, ExecRequest>> srv_rw =
      std::make_shared<grpc::ServerAsyncReaderWriter<ExecResponse, ExecRequest>>(srv_ctx.get());

  service_->RequestExec(srv_ctx.get(), srv_rw.get(), server_cq_.get(), server_cq_.get(),
                        srv_ctx.get());

  // Client creates a new stub.
  std::unique_ptr<grpc::ClientContext> cli_ctx = std::make_unique<grpc::ClientContext>();
  std::unique_ptr<grpc::ClientAsyncReaderWriter<ExecRequest, ExecResponse>> cli_rw;

  cli_rw = stub_->AsyncExec(cli_ctx.get(), client_cq_.get(), cli_ctx.get());

  // The read side of the server exec routine should see that the subprocess
  // has exited.
  ASSERT_GRPC_CQ_NEXT(server_cq_, srv_ctx.get(), true);

  ExecReadCallData<FakePlatform>* server_call_data =
      new ExecReadCallData<FakePlatform>(srv_ctx, srv_rw, 0, 0);
  server_call_data->platform_interface_.SetKillPidReturn(-1);

  // Client sends something to subprocess stdin.
  ASSERT_GRPC_CQ_NEXT(client_cq_, cli_ctx.get(), true);

  ExecRequest exec_request;
  exec_request.clear_argv();
  exec_request.clear_env_vars();
  exec_request.clear_std_in();

  cli_rw->Write(exec_request, &exec_request);

  // The server will get the request, realize the subprocess has exited, and
  // delete itself.  The only indication of the failure will be that the
  // reference count to the ServerAsyncReaderWriter decrements.
  size_t initial_use_count = srv_rw.use_count();
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);
  size_t final_use_count = srv_rw.use_count();

  ASSERT_LT(final_use_count, initial_use_count);

  // Issue a finish on behalf of the server.
  srv_rw->Finish(grpc::Status::OK, srv_rw.get());
  ASSERT_GRPC_CQ_NEXT(server_cq_, srv_rw.get(), true);

  // Cleanup the client reader-writer.
  ASSERT_GRPC_CQ_NEXT(client_cq_, &exec_request, true);
  cli_rw->WritesDone(&exec_request);

  ASSERT_GRPC_CQ_NEXT(client_cq_, &exec_request, true);
  grpc::Status grpc_status;
  cli_rw->Finish(&grpc_status, &exec_request);

  ASSERT_GRPC_CQ_NEXT(client_cq_, &exec_request, true);
  ASSERT_TRUE(grpc_status.ok()) << grpc_status.error_message();
}

TEST_F(AsyncEndToEndTest, Server_ExecWrite_WriteUntilChildExits) {
  // Exec service boilerplate.
  std::shared_ptr<grpc::ServerContext> srv_ctx = std::make_shared<grpc::ServerContext>();
  std::shared_ptr<grpc::ServerAsyncReaderWriter<ExecResponse, ExecRequest>> srv_rw =
      std::make_shared<grpc::ServerAsyncReaderWriter<ExecResponse, ExecRequest>>(srv_ctx.get());

  service_->RequestExec(srv_ctx.get(), srv_rw.get(), server_cq_.get(), server_cq_.get(),
                        srv_ctx.get());

  // Client creates a new stub.
  std::unique_ptr<grpc::ClientContext> cli_ctx = std::make_unique<grpc::ClientContext>();
  std::unique_ptr<grpc::ClientAsyncReaderWriter<ExecRequest, ExecResponse>> cli_rw;

  cli_rw = stub_->AsyncExec(cli_ctx.get(), client_cq_.get(), cli_ctx.get());

  // The write side of the server exec routine will poll the child pid and see
  // that it has exited.
  ASSERT_GRPC_CQ_NEXT(server_cq_, srv_ctx.get(), true);

  ExecWriteCallData<FakePlatform>* server_call_data =
      new ExecWriteCallData<FakePlatform>(srv_ctx, srv_rw, 0, 0, 0);

  // Client reads from the server.
  ASSERT_GRPC_CQ_NEXT(client_cq_, cli_ctx.get(), true);

  ExecResponse exec_response;
  cli_rw->Read(&exec_response, &exec_response);

  // The server will finish and delete itself.
  ASSERT_GRPC_CQ_NEXT(server_cq_, server_call_data, true);
  server_call_data->Proceed(true);

  // The client will get the initial server write and issue another read.
  ASSERT_GRPC_CQ_NEXT(client_cq_, &exec_response, true);
  cli_rw->Read(&exec_response, &exec_response);

  // The client should see that the server has finished and request the finish
  // status.
  ASSERT_GRPC_CQ_NEXT(client_cq_, &exec_response, false);

  grpc::Status grpc_status;

  cli_rw->Finish(&grpc_status, &exec_response);
  ASSERT_GRPC_CQ_NEXT(client_cq_, &exec_response, true);

  ASSERT_TRUE(grpc_status.ok()) << grpc_status.error_message();
}
