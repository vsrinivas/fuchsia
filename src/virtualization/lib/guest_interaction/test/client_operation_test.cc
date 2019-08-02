// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <src/lib/fxl/logging.h>

#include "src/virtualization/lib/guest_interaction/client/client_operation_state.h"
#include "test_lib.h"

#include <grpc++/grpc++.h>

// Client Get State Machine Test Cases
//
// 1. Client requests a file that does not exist on the server.
// 2. Client requests a file that is sent unfragmented.
// 3. Client requests a file that is sent as multiple fragments.
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

// Client Put State Machine Test Cases
//
// 1. Client fails to read from the open file.
// 2. The file to be pushed is sent in a single fragment.
// 3. The file to be pushed is sent in multiple fragments.
// 4. gRPC fails while the client is transferring the file.

TEST_F(AsyncEndToEndTest, PutReadFails) {
  ResetStub();
  // Accounting bits for managing CompletionQueue state.
  void* tag;
  bool cq_status;

  // Create a service that can accept incoming Get requests.
  OperationStatus operation_status;
  grpc::ServerContext srv_ctx;
  grpc::ServerAsyncReader<PutResponse, PutRequest> request_reader(&srv_ctx);

  service_->RequestPut(&srv_ctx, &request_reader, server_cq_.get(), server_cq_.get(), this);

  // Create components required to perform a client Put request.
  int32_t fake_fd = 0;
  PutCallData<FakePlatform>* client_call_data = new PutCallData<FakePlatform>(
      fake_fd, "/some/dest",
      [&operation_status](OperationStatus status) { operation_status = status; });

  client_call_data->writer_ =
      stub_->AsyncPut(&(client_call_data->ctx_), &(client_call_data->response_), client_cq_.get(),
                      client_call_data);

  // Server should get the client request.
  server_cq_->Next(&tag, &cq_status);
  PutRequest put_request;
  request_reader.Read(&put_request, nullptr);

  // Set the mock up to inform the client that the source file exists but
  // fails to open.
  client_call_data->platform_interface_.SetFileExistsReturn(true);
  client_call_data->platform_interface_.SetOpenFileReturn(1);
  client_call_data->platform_interface_.SetReadFileReturn(-1);

  // Wait for the request to go out.
  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(cq_status);

  // Server CompletionQueue should get the client's finish message.
  server_cq_->Next(&tag, &cq_status);
  ASSERT_FALSE(cq_status);

  PutResponse put_response;
  put_response.set_status(OperationStatus::OK);
  request_reader.Finish(put_response, grpc::Status::OK, nullptr);

  // Client should get the server's finish message and delete itself.
  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(cq_status);

  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(cq_status);

  // The client sets the operation status in the callback.
  ASSERT_EQ(operation_status, OperationStatus::CLIENT_FILE_READ_FAILURE);
}

TEST_F(AsyncEndToEndTest, PutOneFragment) {
  ResetStub();
  // Accounting bits for managing CompletionQueue state.
  void* tag;
  bool cq_status;

  // Create a service that can accept incoming Get requests.
  OperationStatus operation_status;
  grpc::ServerContext srv_ctx;
  grpc::ServerAsyncReader<PutResponse, PutRequest> request_reader(&srv_ctx);

  service_->RequestPut(&srv_ctx, &request_reader, server_cq_.get(), server_cq_.get(), this);

  // Create components required to perform a client Put request.
  int32_t fake_fd = 0;
  PutCallData<FakePlatform>* client_call_data = new PutCallData<FakePlatform>(
      fake_fd, "/some/dest",
      [&operation_status](OperationStatus status) { operation_status = status; });

  client_call_data->writer_ =
      stub_->AsyncPut(&(client_call_data->ctx_), &(client_call_data->response_), client_cq_.get(),
                      client_call_data);

  // Server should get the client request.
  server_cq_->Next(&tag, &cq_status);
  PutRequest put_request;
  request_reader.Read(&put_request, nullptr);

  // Set the mock up to inform the client that the source file exists but
  // fails to open.
  client_call_data->platform_interface_.SetFileExistsReturn(true);
  client_call_data->platform_interface_.SetOpenFileReturn(1);
  client_call_data->platform_interface_.SetReadFileContents("test");

  // Wait for the request to go out.
  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(cq_status);

  // Server CompletionQueue should get the client's message and request another
  // file fragment.
  server_cq_->Next(&tag, &cq_status);
  ASSERT_TRUE(cq_status);
  request_reader.Read(&put_request, nullptr);

  // Client hits the end of the file and finishes.
  client_call_data->platform_interface_.SetReadFileContents("");
  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(cq_status);

  // Server gets the finish and finishes with the client.
  server_cq_->Next(&tag, &cq_status);
  ASSERT_FALSE(cq_status);
  PutResponse put_response;
  put_response.set_status(OperationStatus::OK);
  request_reader.Finish(put_response, grpc::Status::OK, nullptr);

  // Client should get the server's finish message and delete itself.
  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(cq_status);

  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(cq_status);

  // The client sets the operation status in the callback.
  ASSERT_EQ(operation_status, OperationStatus::OK);
}

TEST_F(AsyncEndToEndTest, PutMultipleFragments) {
  ResetStub();
  // Accounting bits for managing CompletionQueue state.
  void* tag;
  bool cq_status;

  // Create a service that can accept incoming Get requests.
  OperationStatus operation_status;
  grpc::ServerContext srv_ctx;
  grpc::ServerAsyncReader<PutResponse, PutRequest> request_reader(&srv_ctx);

  service_->RequestPut(&srv_ctx, &request_reader, server_cq_.get(), server_cq_.get(), this);

  // Create components required to perform a client Put request.
  int32_t fake_fd = 0;
  PutCallData<FakePlatform>* client_call_data = new PutCallData<FakePlatform>(
      fake_fd, "/some/dest",
      [&operation_status](OperationStatus status) { operation_status = status; });

  client_call_data->writer_ =
      stub_->AsyncPut(&(client_call_data->ctx_), &(client_call_data->response_), client_cq_.get(),
                      client_call_data);

  // Server should get the client request.
  server_cq_->Next(&tag, &cq_status);
  PutRequest put_request;
  request_reader.Read(&put_request, nullptr);

  // Set the mock up to inform the client that the source file exists but
  // fails to open.
  client_call_data->platform_interface_.SetFileExistsReturn(true);
  client_call_data->platform_interface_.SetOpenFileReturn(1);
  client_call_data->platform_interface_.SetReadFileContents("test");

  // Wait for the request to go out.
  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(cq_status);

  // Server CompletionQueue should get the client's message and request another
  // file fragment.
  server_cq_->Next(&tag, &cq_status);
  ASSERT_TRUE(cq_status);
  request_reader.Read(&put_request, nullptr);

  // Send a second file fragment.
  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(cq_status);

  // Server CompletionQueue should get the client's message and request another
  // file fragment.
  server_cq_->Next(&tag, &cq_status);
  ASSERT_TRUE(cq_status);
  request_reader.Read(&put_request, nullptr);

  // Client hits the end of the file and writes done.
  client_call_data->platform_interface_.SetReadFileContents("");
  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(cq_status);

  // Server gets the finish and finishes with the client.
  server_cq_->Next(&tag, &cq_status);
  ASSERT_FALSE(cq_status);
  PutResponse put_response;
  put_response.set_status(OperationStatus::OK);
  request_reader.Finish(put_response, grpc::Status::OK, nullptr);

  // Client should get the server's finish message and delete itself.
  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(cq_status);

  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(cq_status);

  // The client sets the operation status in the callback.
  ASSERT_EQ(operation_status, OperationStatus::OK);
}

TEST_F(AsyncEndToEndTest, PutGrpcFailure) {
  ResetStub();
  // Accounting bits for managing CompletionQueue state.
  void* tag;
  bool cq_status;

  // Create a service that can accept incoming Get requests.
  OperationStatus operation_status;
  grpc::ServerContext srv_ctx;
  grpc::ServerAsyncReader<PutResponse, PutRequest> request_reader(&srv_ctx);

  service_->RequestPut(&srv_ctx, &request_reader, server_cq_.get(), server_cq_.get(), this);

  // Create components required to perform a client Put request.
  int32_t fake_fd = 0;
  PutCallData<FakePlatform>* client_call_data = new PutCallData<FakePlatform>(
      fake_fd, "/some/dest",
      [&operation_status](OperationStatus status) { operation_status = status; });

  client_call_data->writer_ =
      stub_->AsyncPut(&(client_call_data->ctx_), &(client_call_data->response_), client_cq_.get(),
                      client_call_data);

  // Server should get the client request.
  server_cq_->Next(&tag, &cq_status);
  PutRequest put_request;
  request_reader.Read(&put_request, nullptr);

  // Set the mock up to inform the client that the source file exists but
  // fails to open.
  client_call_data->platform_interface_.SetFileExistsReturn(true);
  client_call_data->platform_interface_.SetOpenFileReturn(1);
  client_call_data->platform_interface_.SetReadFileContents("test");

  // Wait for the request to go out.
  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(cq_status);

  // Server CompletionQueue should get the client's message.
  server_cq_->Next(&tag, &cq_status);
  request_reader.Read(&put_request, nullptr);

  // Inject a gRPC failure into the client procedure.
  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(false);

  // The client sets the operation status in the callback.
  ASSERT_EQ(operation_status, OperationStatus::GRPC_FAILURE);
}

// Client Exec State Machine Tests
//
// 1. gRPC connection immediately fails.
// 2. stdin is successfully sent to the child until the stdin source is
//    exhausted.
// 3. Server sends stdout/stderr and then terminates the transfer.

TEST_F(AsyncEndToEndTest, Client_Exec_ImmediateFailure) {
  ResetStub();
  // Accounting bits for managing CompletionQueue state.
  void* tag;
  bool cq_status;

  // Create a service that can accept incoming Exec requests.
  OperationStatus operation_status;
  int32_t return_code;
  grpc::ServerContext srv_ctx;
  grpc::ServerAsyncReaderWriter<ExecResponse, ExecRequest> rw(&srv_ctx);

  service_->RequestExec(&srv_ctx, &rw, server_cq_.get(), server_cq_.get(), this);

  // Create components required to perform a client Exec request.
  std::string test_argv = "echo hello";
  std::map<std::string, std::string> empty_env;
  ExecCallData<FakePlatform>* client_call_data = new ExecCallData<FakePlatform>(
      test_argv, empty_env, 0, 1, 2,
      [&operation_status, &return_code](OperationStatus status, int32_t ret_code) {
        operation_status = status;
        return_code = ret_code;
      });
  client_call_data->rw_ =
      stub_->AsyncExec(client_call_data->ctx_.get(), client_cq_.get(), client_call_data);

  // Server should get the new stub request.
  server_cq_->Next(&tag, &cq_status);

  // Inject a failure into the client.
  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(false);

  // The client sets the operation status in the callback.
  ASSERT_EQ(operation_status, OperationStatus::GRPC_FAILURE);
}

TEST_F(AsyncEndToEndTest, Client_ExecWrite_Test) {
  ResetStub();
  // Accounting bits for managing CompletionQueue state.
  void* tag;
  bool cq_status;

  // Create a service that can accept incoming Exec requests.
  grpc::ServerContext srv_ctx;
  std::shared_ptr<grpc::ClientContext> cli_ctx = std::make_shared<grpc::ClientContext>();
  grpc::ServerAsyncReaderWriter<ExecResponse, ExecRequest> srv_rw(&srv_ctx);
  std::shared_ptr<grpc::ClientAsyncReaderWriter<ExecRequest, ExecResponse>> cli_rw;

  service_->RequestExec(&srv_ctx, &srv_rw, server_cq_.get(), server_cq_.get(), this);

  // Create components required to perform a client Exec request.
  std::string empty_argv = "echo hello";
  std::vector<ExecEnv> empty_env;
  ExecWriteCallData<FakePlatform>* client_call_data;
  cli_rw = stub_->AsyncExec(cli_ctx.get(), client_cq_.get(), &client_call_data);

  // Clear the initial event that is generated by the stub creation.  This
  // would normally be handled by the top-level ExecCallData.
  client_cq_->Next(&tag, &cq_status);

  client_call_data = new ExecWriteCallData<FakePlatform>(empty_argv, empty_env, 0, cli_ctx, cli_rw);

  // Server should get the new stub request and begin reading.
  ExecRequest exec_request;
  server_cq_->Next(&tag, &cq_status);
  srv_rw.Read(&exec_request, nullptr);

  // Client should read successfully from stdin and send a message to the
  // server.
  client_call_data->platform_interface_.SetReadFileContents("test");
  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(cq_status);

  // Server should continue reading.
  server_cq_->Next(&tag, &cq_status);
  srv_rw.Read(&exec_request, nullptr);

  // Client should hit end of file on stdin.
  client_call_data->platform_interface_.SetReadFileContents("");
  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(cq_status);

  // Server should finish.
  server_cq_->Next(&tag, &cq_status);
  srv_rw.Finish(grpc::Status::OK, nullptr);

  // Client should get the finish message and delete itself.
  int32_t initial_use_count = cli_rw.use_count();

  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(cq_status);

  int32_t final_use_count = cli_rw.use_count();

  // The component that writes stdin silently deletes itself.  The read
  // component is responsible for reporting final status.  Check the shared_ptr
  // reference count to ensure that the client has deleted itself.
  ASSERT_LT(final_use_count, initial_use_count);
}

TEST_F(AsyncEndToEndTest, Client_ExecRead_Test) {
  ResetStub();
  // Accounting bits for managing CompletionQueue state.
  void* tag;
  bool cq_status;

  // Create a service that can accept incoming Exec requests.
  OperationStatus operation_status;
  int32_t return_code;
  grpc::ServerContext srv_ctx;
  std::shared_ptr<grpc::ClientContext> cli_ctx = std::make_shared<grpc::ClientContext>();
  grpc::ServerAsyncReaderWriter<ExecResponse, ExecRequest> srv_rw(&srv_ctx);
  std::shared_ptr<grpc::ClientAsyncReaderWriter<ExecRequest, ExecResponse>> cli_rw;

  service_->RequestExec(&srv_ctx, &srv_rw, server_cq_.get(), server_cq_.get(), this);

  // Create components required to perform a client Exec request.
  ExecReadCallData<FakePlatform>* client_call_data;
  cli_rw = stub_->AsyncExec(cli_ctx.get(), client_cq_.get(), &client_call_data);

  // Clear the inital event that is generated by the stub creation.  This would
  // normally be handled by the top-level ExecCallData.
  client_cq_->Next(&tag, &cq_status);

  client_call_data = new ExecReadCallData<FakePlatform>(
      0, 0, cli_ctx, cli_rw,
      [&operation_status, &return_code](OperationStatus status, int32_t ret_code) {
        operation_status = status;
        return_code = ret_code;
      });

  // Server should get the new stub request and immediately finish.
  server_cq_->Next(&tag, &cq_status);

  int32_t ret_code = 1234;

  ExecResponse exec_response;
  exec_response.clear_std_out();
  exec_response.clear_std_err();
  exec_response.set_ret_code(ret_code);
  srv_rw.WriteAndFinish(exec_response, grpc::WriteOptions(), grpc::Status::OK, nullptr);

  // Client should get the message.
  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(cq_status);

  // Client should get the finish message.
  client_cq_->Next(&tag, &cq_status);
  ASSERT_FALSE(cq_status);
  client_call_data->Proceed(cq_status);

  // Client should run the callback and clean up.
  client_cq_->Next(&tag, &cq_status);
  client_call_data->Proceed(cq_status);

  // The client sets the operation status in the callback.
  ASSERT_EQ(operation_status, OperationStatus::OK);
  ASSERT_EQ(return_code, ret_code);
}
