// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include "src/lib/testing/predicates/status.h"
#include "src/virtualization/lib/guest_interaction/client/client_operation_state.h"
#include "src/virtualization/lib/guest_interaction/test/operation_test_lib.h"

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
  // Create a service that can accept incoming Get requests.
  GetRequest incoming_request;
  grpc::ServerContext srv_ctx;
  grpc::ServerAsyncWriter<GetResponse> response_writer(&srv_ctx);

  service_->RequestGet(&srv_ctx, &incoming_request, &response_writer, server_cq_.get(),
                       server_cq_.get(), &srv_ctx);

  // Create components required to perform a client Get request.
  zx_status_t operation_status = ZX_OK;
  GetRequest get_request;
  get_request.set_source("/some/bogus/path");

  uint32_t fake_fd = 0;
  GetCallData<FakePlatform>* client_call_data = new GetCallData<FakePlatform>(
      fake_fd, [&operation_status](zx_status_t status) { operation_status = status; });

  client_call_data->reader_ =
      stub_->AsyncGet(&(client_call_data->ctx_), get_request, client_cq_.get(), client_call_data);

  // Wait for the request to go out.
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, true);
  client_call_data->Proceed(true);

  // Server CompletionQueue should get the client request.
  ASSERT_GRPC_CQ_NEXT(server_cq_, &srv_ctx, true);

  GetResponse outgoing_response;
  outgoing_response.clear_data();
  outgoing_response.set_status(OperationStatus::SERVER_MISSING_FILE_FAILURE);
  response_writer.Write(outgoing_response, &outgoing_response);

  // Client should get the server's message and then wait for the server to
  // call Finish.
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, true);
  client_call_data->Proceed(true);

  // Server finishes.
  ASSERT_GRPC_CQ_NEXT(server_cq_, &outgoing_response, true);
  response_writer.Finish(grpc::Status::OK, &outgoing_response);
  ASSERT_GRPC_CQ_NEXT(server_cq_, &outgoing_response, true);

  // Client gets final status from server, runs the callback, and then
  // deletes itself.
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, true);
  client_call_data->Proceed(true);

  // The client sets the operation status in the callback.
  ASSERT_STATUS(operation_status, ZX_ERR_NOT_FOUND);
}

TEST_F(AsyncEndToEndTest, SmallFile) {
  // Create a service that can accept incoming Get requests.
  GetRequest incoming_request;
  grpc::ServerContext srv_ctx;
  grpc::ServerAsyncWriter<GetResponse> response_writer(&srv_ctx);

  service_->RequestGet(&srv_ctx, &incoming_request, &response_writer, server_cq_.get(),
                       server_cq_.get(), &srv_ctx);

  // Create components required to perform a client Get request.
  zx_status_t operation_status = ZX_ERR_PEER_CLOSED;
  GetRequest get_request;
  get_request.set_source("/some/small/file");

  uint32_t fake_fd = 0;
  GetCallData<FakePlatform>* client_call_data = new GetCallData<FakePlatform>(
      fake_fd, [&operation_status](zx_status_t status) { operation_status = status; });
  client_call_data->reader_ =
      stub_->AsyncGet(&(client_call_data->ctx_), get_request, client_cq_.get(), client_call_data);

  // Wait for the request to go out.
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, true);
  client_call_data->Proceed(true);

  // The mock will notify the client that all writes are successful.
  client_call_data->platform_interface_.SetOpenFileReturn(1);
  client_call_data->platform_interface_.SetWriteFileReturn(1);

  // Server CompletionQueue should get the client request.
  // Send back a short message.
  ASSERT_GRPC_CQ_NEXT(server_cq_, &srv_ctx, true);

  GetResponse outgoing_response;
  outgoing_response.set_data("Small file contents");
  outgoing_response.set_status(OperationStatus::OK);
  response_writer.Write(outgoing_response, &response_writer);

  // Client should get the server's message and then wait for the server to
  // send more data.
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, true);
  client_call_data->Proceed(true);

  // Server finishes, indicating that there is no more data.
  ASSERT_GRPC_CQ_NEXT(server_cq_, &response_writer, true);
  response_writer.Finish(grpc::Status::OK, &response_writer);
  ASSERT_GRPC_CQ_NEXT(server_cq_, &response_writer, true);

  // Client should get a bad status from the queue and then wait for the query
  // of the finish status.
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, false);
  client_call_data->Proceed(false);

  // Client gets final status, runs the callback, and then deletes itself.
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, true);
  client_call_data->Proceed(true);

  // The client sets the operation status in the callback.
  ASSERT_OK(operation_status);
}

TEST_F(AsyncEndToEndTest, LargeFile) {
  // Create a service that can accept incoming Get requests.
  GetRequest incoming_request;
  grpc::ServerContext srv_ctx;
  grpc::ServerAsyncWriter<GetResponse> response_writer(&srv_ctx);

  service_->RequestGet(&srv_ctx, &incoming_request, &response_writer, server_cq_.get(),
                       server_cq_.get(), &response_writer);

  // Create components required to perform a client Get request.
  zx_status_t operation_status = ZX_ERR_PEER_CLOSED;
  GetRequest get_request;
  get_request.set_source("/some/small/file");

  uint32_t fake_fd = 0;
  GetCallData<FakePlatform>* client_call_data = new GetCallData<FakePlatform>(
      fake_fd, [&operation_status](zx_status_t status) { operation_status = status; });
  client_call_data->reader_ =
      stub_->AsyncGet(&(client_call_data->ctx_), get_request, client_cq_.get(), client_call_data);

  // Wait for the request to go out.
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, true);
  client_call_data->Proceed(true);

  // The mock will notify the client that all writes are successful.
  client_call_data->platform_interface_.SetOpenFileReturn(1);
  client_call_data->platform_interface_.SetWriteFileReturn(1);

  // Server CompletionQueue should get the client request.
  // Send back a short message.
  ASSERT_GRPC_CQ_NEXT(server_cq_, &response_writer, true);

  GetResponse outgoing_response;
  outgoing_response.set_data("large file contents");
  outgoing_response.set_status(OperationStatus::OK);
  response_writer.Write(outgoing_response, &response_writer);

  // Client should get the server's message and then wait for the server to
  // send more data.
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, true);
  client_call_data->Proceed(true);

  // Server CompletionQueue should get the client request.
  // Send back a short message.
  ASSERT_GRPC_CQ_NEXT(server_cq_, &response_writer, true);

  outgoing_response.set_data("large file contents");
  outgoing_response.set_status(OperationStatus::OK);
  response_writer.Write(outgoing_response, &response_writer);

  // Client should get the server's message and then wait for the server to
  // send more data.
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, true);
  client_call_data->Proceed(true);

  // Server finishes, indicating that there is no more data.
  ASSERT_GRPC_CQ_NEXT(server_cq_, &response_writer, true);
  response_writer.Finish(grpc::Status::OK, &response_writer);
  ASSERT_GRPC_CQ_NEXT(server_cq_, &response_writer, true);

  // Client should get a bad status from the queue and then wait for the query
  // the finish status.
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, false);
  client_call_data->Proceed(false);

  // Client gets final status, runs the callback, and then deletes itself.
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, true);
  client_call_data->Proceed(true);

  // The client sets the operation status in the callback.
  ASSERT_OK(operation_status);
}

TEST_F(AsyncEndToEndTest, BrokenWrite) {
  // Create a service that can accept incoming Get requests.
  GetRequest incoming_request;
  grpc::ServerContext srv_ctx;
  grpc::ServerAsyncWriter<GetResponse> response_writer(&srv_ctx);

  service_->RequestGet(&srv_ctx, &incoming_request, &response_writer, server_cq_.get(),
                       server_cq_.get(), &response_writer);

  // Create components required to perform a client Get request.
  zx_status_t operation_status = ZX_ERR_PEER_CLOSED;
  GetRequest get_request;
  get_request.set_source("/some/small/file");

  uint32_t fake_fd = 0;
  GetCallData<FakePlatform>* client_call_data = new GetCallData<FakePlatform>(
      fake_fd, [&operation_status](zx_status_t status) { operation_status = status; });
  client_call_data->reader_ =
      stub_->AsyncGet(&(client_call_data->ctx_), get_request, client_cq_.get(), client_call_data);

  // Wait for the request to go out.
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, true);
  client_call_data->Proceed(true);

  // The mock will notify the client that write has failed.
  client_call_data->platform_interface_.SetOpenFileReturn(1);
  client_call_data->platform_interface_.SetOpenFileReturn(1);
  client_call_data->platform_interface_.SetWriteFileReturn(-1);

  // Server CompletionQueue should get the client request.
  // Send back a short message.
  ASSERT_GRPC_CQ_NEXT(server_cq_, &response_writer, true);

  GetResponse outgoing_response;
  outgoing_response.set_data("Small file contents");
  outgoing_response.set_status(OperationStatus::OK);
  response_writer.Write(outgoing_response, &response_writer);

  // Client should get the server's message, fail to write, and then finish.
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, true);
  client_call_data->Proceed(true);

  // Server finishes, indicating that there is no more data.
  ASSERT_GRPC_CQ_NEXT(server_cq_, &response_writer, true);
  response_writer.Finish(grpc::Status::OK, &response_writer);
  ASSERT_GRPC_CQ_NEXT(server_cq_, &response_writer, true);

  // Client finishes and deletes itself.
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, true);
  client_call_data->Proceed(true);

  // The client sets the operation status in the callback.
  ASSERT_STATUS(operation_status, ZX_ERR_IO);
}

TEST_F(AsyncEndToEndTest, GrpcFailure) {
  // Create a service that can accept incoming Get requests.
  GetRequest incoming_request;
  grpc::ServerContext srv_ctx;
  grpc::ServerAsyncWriter<GetResponse> response_writer(&srv_ctx);

  service_->RequestGet(&srv_ctx, &incoming_request, &response_writer, server_cq_.get(),
                       server_cq_.get(), &response_writer);

  // Create components required to perform a client Get request.
  zx_status_t operation_status = ZX_OK;
  GetRequest get_request;
  get_request.set_source("/some/small/file");

  uint32_t fake_fd = 0;
  GetCallData<FakePlatform>* client_call_data = new GetCallData<FakePlatform>(
      fake_fd, [&operation_status](zx_status_t status) { operation_status = status; });
  client_call_data->reader_ =
      stub_->AsyncGet(&(client_call_data->ctx_), get_request, client_cq_.get(), client_call_data);

  // Wait for the request to go out and then tell the client that it was
  // unsuccessful.
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, true);
  client_call_data->Proceed(false);

  // Server finishes, indicating that there is no more data.
  ASSERT_GRPC_CQ_NEXT(server_cq_, &response_writer, true);
  response_writer.Finish(grpc::Status::OK, &response_writer);
  ASSERT_GRPC_CQ_NEXT(server_cq_, &response_writer, true);

  // Client finishes and deletes itself.
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, true);
  client_call_data->Proceed(true);

  // The client sets the operation status in the callback.
  ASSERT_STATUS(operation_status, ZX_ERR_PEER_CLOSED);
}

// Client Put State Machine Test Cases
//
// 1. Client fails to read from the open file.
// 2. The file to be pushed is sent in a single fragment.
// 3. The file to be pushed is sent in multiple fragments.
// 4. gRPC fails while the client is transferring the file.

TEST_F(AsyncEndToEndTest, PutReadFails) {
  // Create a service that can accept incoming Get requests.
  zx_status_t operation_status = ZX_OK;
  grpc::ServerContext srv_ctx;
  grpc::ServerAsyncReader<PutResponse, PutRequest> request_reader(&srv_ctx);

  service_->RequestPut(&srv_ctx, &request_reader, server_cq_.get(), server_cq_.get(), &srv_ctx);

  // Create components required to perform a client Put request.
  int32_t fake_fd = 0;
  PutCallData<FakePlatform>* client_call_data = new PutCallData<FakePlatform>(
      fake_fd, "/some/dest",
      [&operation_status](zx_status_t status) { operation_status = status; });

  client_call_data->writer_ =
      stub_->AsyncPut(&(client_call_data->ctx_), &(client_call_data->response_), client_cq_.get(),
                      client_call_data);

  // Server should get the client request.
  ASSERT_GRPC_CQ_NEXT(server_cq_, &srv_ctx, true);
  PutRequest put_request;
  request_reader.Read(&put_request, &request_reader);

  // Set the mock up to inform the client that the source file exists but
  // fails to open.
  client_call_data->platform_interface_.SetFileExistsReturn(true);
  client_call_data->platform_interface_.SetOpenFileReturn(1);
  client_call_data->platform_interface_.SetReadFileReturn(-1);

  // Wait for the request to go out.
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, true);
  client_call_data->Proceed(true);

  // Server CompletionQueue should get the client's finish message.
  ASSERT_GRPC_CQ_NEXT(server_cq_, &request_reader, false);

  PutResponse put_response;
  put_response.set_status(OperationStatus::OK);
  request_reader.Finish(put_response, grpc::Status::OK, &request_reader);

  // Client should get the server's finish message and delete itself.
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, true);
  client_call_data->Proceed(true);
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, true);
  client_call_data->Proceed(true);

  ASSERT_GRPC_CQ_NEXT(server_cq_, &request_reader, true);

  // The client sets the operation status in the callback.
  ASSERT_STATUS(operation_status, ZX_ERR_IO);
}

TEST_F(AsyncEndToEndTest, PutOneFragment) {
  // Create a service that can accept incoming Get requests.
  zx_status_t operation_status = ZX_ERR_IO;
  grpc::ServerContext srv_ctx;
  grpc::ServerAsyncReader<PutResponse, PutRequest> request_reader(&srv_ctx);

  service_->RequestPut(&srv_ctx, &request_reader, server_cq_.get(), server_cq_.get(), &srv_ctx);

  // Create components required to perform a client Put request.
  int32_t fake_fd = 0;
  PutCallData<FakePlatform>* client_call_data = new PutCallData<FakePlatform>(
      fake_fd, "/some/dest",
      [&operation_status](zx_status_t status) { operation_status = status; });

  client_call_data->writer_ =
      stub_->AsyncPut(&(client_call_data->ctx_), &(client_call_data->response_), client_cq_.get(),
                      client_call_data);

  // Server should get the client request.
  ASSERT_GRPC_CQ_NEXT(server_cq_, &srv_ctx, true);
  PutRequest put_request;
  request_reader.Read(&put_request, &request_reader);

  // Set the mock up to inform the client that the source file exists but
  // fails to open.
  client_call_data->platform_interface_.SetFileExistsReturn(true);
  client_call_data->platform_interface_.SetOpenFileReturn(1);
  client_call_data->platform_interface_.SetReadFileContents("test");

  // Wait for the request to go out.
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, true);
  client_call_data->Proceed(true);

  // Server CompletionQueue should get the client's message and request another
  // file fragment.
  ASSERT_GRPC_CQ_NEXT(server_cq_, &request_reader, true);
  request_reader.Read(&put_request, &request_reader);

  // Client hits the end of the file and finishes.
  client_call_data->platform_interface_.SetReadFileContents("");
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, true);
  client_call_data->Proceed(true);

  // Server gets the finish and finishes with the client.
  ASSERT_GRPC_CQ_NEXT(server_cq_, &request_reader, false);
  PutResponse put_response;
  put_response.set_status(OperationStatus::OK);
  request_reader.Finish(put_response, grpc::Status::OK, &request_reader);

  // Client should get the server's finish message and delete itself.
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, true);
  client_call_data->Proceed(true);
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, true);
  client_call_data->Proceed(true);

  ASSERT_GRPC_CQ_NEXT(server_cq_, &request_reader, true);

  // The client sets the operation status in the callback.
  ASSERT_OK(operation_status);
}

TEST_F(AsyncEndToEndTest, PutMultipleFragments) {
  // Create a service that can accept incoming Get requests.
  zx_status_t operation_status = ZX_ERR_IO;
  grpc::ServerContext srv_ctx;
  grpc::ServerAsyncReader<PutResponse, PutRequest> request_reader(&srv_ctx);

  service_->RequestPut(&srv_ctx, &request_reader, server_cq_.get(), server_cq_.get(), &srv_ctx);

  // Create components required to perform a client Put request.
  int32_t fake_fd = 0;
  PutCallData<FakePlatform>* client_call_data = new PutCallData<FakePlatform>(
      fake_fd, "/some/dest",
      [&operation_status](zx_status_t status) { operation_status = status; });

  client_call_data->writer_ =
      stub_->AsyncPut(&(client_call_data->ctx_), &(client_call_data->response_), client_cq_.get(),
                      client_call_data);

  // Server should get the client request.
  ASSERT_GRPC_CQ_NEXT(server_cq_, &srv_ctx, true);
  PutRequest put_request;
  request_reader.Read(&put_request, &request_reader);

  // Set the mock up to inform the client that the source file exists but
  // fails to open.
  client_call_data->platform_interface_.SetFileExistsReturn(true);
  client_call_data->platform_interface_.SetOpenFileReturn(1);
  client_call_data->platform_interface_.SetReadFileContents("test");

  // Wait for the request to go out.
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, true);
  client_call_data->Proceed(true);

  // Server CompletionQueue should get the client's message and request another
  // file fragment.
  ASSERT_GRPC_CQ_NEXT(server_cq_, &request_reader, true);
  request_reader.Read(&put_request, &request_reader);

  // Send a second file fragment.
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, true);
  client_call_data->Proceed(true);

  // Server CompletionQueue should get the client's message and request another
  // file fragment.
  ASSERT_GRPC_CQ_NEXT(server_cq_, &request_reader, true);
  request_reader.Read(&put_request, &request_reader);

  // Client hits the end of the file and writes done.
  client_call_data->platform_interface_.SetReadFileContents("");
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, true);
  client_call_data->Proceed(true);

  // Server gets the finish and finishes with the client.
  ASSERT_GRPC_CQ_NEXT(server_cq_, &request_reader, false);
  PutResponse put_response;
  put_response.set_status(OperationStatus::OK);
  request_reader.Finish(put_response, grpc::Status::OK, &request_reader);

  // Client should get the server's finish message and delete itself.
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, true);
  client_call_data->Proceed(true);
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, true);
  client_call_data->Proceed(true);

  ASSERT_GRPC_CQ_NEXT(server_cq_, &request_reader, true);

  // The client sets the operation status in the callback.
  ASSERT_OK(operation_status);
}

TEST_F(AsyncEndToEndTest, PutGrpcFailure) {
  // Create a service that can accept incoming Get requests.
  zx_status_t operation_status = ZX_OK;
  grpc::ServerContext srv_ctx;
  grpc::ServerAsyncReader<PutResponse, PutRequest> request_reader(&srv_ctx);

  service_->RequestPut(&srv_ctx, &request_reader, server_cq_.get(), server_cq_.get(), &srv_ctx);

  // Create components required to perform a client Put request.
  int32_t fake_fd = 0;
  PutCallData<FakePlatform>* client_call_data = new PutCallData<FakePlatform>(
      fake_fd, "/some/dest",
      [&operation_status](zx_status_t status) { operation_status = status; });

  client_call_data->writer_ =
      stub_->AsyncPut(&(client_call_data->ctx_), &(client_call_data->response_), client_cq_.get(),
                      client_call_data);

  // Server should get the client request.
  ASSERT_GRPC_CQ_NEXT(server_cq_, &srv_ctx, true);
  PutRequest put_request;
  request_reader.Read(&put_request, &request_reader);

  // Set the mock up to inform the client that the source file exists but
  // fails to open.
  client_call_data->platform_interface_.SetFileExistsReturn(true);
  client_call_data->platform_interface_.SetOpenFileReturn(1);
  client_call_data->platform_interface_.SetReadFileContents("test");

  // Wait for the request to go out.
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, true);
  client_call_data->Proceed(true);

  // Server CompletionQueue should get the client's message.
  ASSERT_GRPC_CQ_NEXT(server_cq_, &request_reader, true);
  request_reader.Read(&put_request, &request_reader);

  // Inject a gRPC failure into the client procedure.
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, true);
  client_call_data->Proceed(false);

  ASSERT_GRPC_CQ_NEXT(server_cq_, &request_reader, false);

  // The client sets the operation status in the callback.
  ASSERT_STATUS(operation_status, ZX_ERR_PEER_CLOSED);
}

// Client Exec State Machine Tests
//
// 1. gRPC connection immediately fails.
// 2. stdin is successfully sent to the child until the stdin source is
//    exhausted.
// 3. Server sends stdout/stderr and then terminates the transfer.

TEST_F(AsyncEndToEndTest, Client_Exec_ImmediateFailure) {
  // Create a service that can accept incoming Exec requests.
  bool operation_status_done = false;
  bool termination_status_done = false;
  grpc::ServerContext srv_ctx;
  grpc::ServerAsyncReaderWriter<ExecResponse, ExecRequest> rw(&srv_ctx);

  service_->RequestExec(&srv_ctx, &rw, server_cq_.get(), server_cq_.get(), &srv_ctx);

  fuchsia::netemul::guest::CommandListenerPtr listener;
  listener.events().OnStarted = [&operation_status_done](zx_status_t status) {
    operation_status_done = true;
    EXPECT_STATUS(status, ZX_ERR_INTERNAL);
    operation_status_done = status;
  };
  listener.events().OnTerminated = [&termination_status_done](zx_status_t status,
                                                              int32_t exit_code) {
    EXPECT_STATUS(status, ZX_ERR_PEER_CLOSED);
    termination_status_done = true;
  };
  std::unique_ptr<ListenerInterface> listener_interface =
      std::make_unique<ListenerInterface>(listener.NewRequest());

  // Create components required to perform a client Exec request.
  std::string test_argv = "echo hello";
  std::map<std::string, std::string> empty_env;
  ExecCallData<FakePlatform>* client_call_data =
      new ExecCallData<FakePlatform>(test_argv, empty_env, 0, 1, 2, std::move(listener_interface));
  client_call_data->rw_ =
      stub_->AsyncExec(client_call_data->ctx_.get(), client_cq_.get(), client_call_data);

  // Server should get the new stub request.
  ASSERT_GRPC_CQ_NEXT(server_cq_, &srv_ctx, true);

  // Inject a failure into the client.
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, true);
  client_call_data->Proceed(false);

  // The client sets the operation status in the callback.
  RunLoopUntil([&operation_status_done, &termination_status_done]() {
    return operation_status_done && termination_status_done;
  });
}

TEST_F(AsyncEndToEndTest, Client_ExecWrite_Test) {
  // Create a service that can accept incoming Exec requests.
  grpc::ServerContext srv_ctx;
  std::shared_ptr<grpc::ClientContext> cli_ctx = std::make_shared<grpc::ClientContext>();
  grpc::ServerAsyncReaderWriter<ExecResponse, ExecRequest> srv_rw(&srv_ctx);
  std::shared_ptr<grpc::ClientAsyncReaderWriter<ExecRequest, ExecResponse>> cli_rw;

  service_->RequestExec(&srv_ctx, &srv_rw, server_cq_.get(), server_cq_.get(), &srv_ctx);

  // Create components required to perform a client Exec request.
  std::string empty_argv = "echo hello";
  std::vector<ExecEnv> empty_env;
  ExecWriteCallData<FakePlatform>* client_call_data;
  cli_rw = stub_->AsyncExec(cli_ctx.get(), client_cq_.get(), cli_ctx.get());

  // Clear the initial event that is generated by the stub creation.  This
  // would normally be handled by the top-level ExecCallData.
  ASSERT_GRPC_CQ_NEXT(client_cq_, cli_ctx.get(), true);

  client_call_data = new ExecWriteCallData<FakePlatform>(empty_argv, empty_env, 0, cli_ctx, cli_rw);

  // Server should get the new stub request and begin reading.
  ExecRequest exec_request;
  ASSERT_GRPC_CQ_NEXT(server_cq_, &srv_ctx, true);
  srv_rw.Read(&exec_request, &exec_request);

  // Client should read successfully from stdin and send a message to the
  // server.
  client_call_data->platform_interface_.SetReadFileContents("test");
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, true);
  client_call_data->Proceed(true);

  // Server should continue reading.
  ASSERT_GRPC_CQ_NEXT(server_cq_, &exec_request, true);
  srv_rw.Read(&exec_request, &exec_request);

  // Client should hit end of file on stdin.
  client_call_data->platform_interface_.SetReadFileContents("");
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, true);
  client_call_data->Proceed(true);

  // Server should finish.
  ASSERT_GRPC_CQ_NEXT(server_cq_, &exec_request, true);
  srv_rw.Finish(grpc::Status::OK, &exec_request);
  ASSERT_GRPC_CQ_NEXT(server_cq_, &exec_request, true);

  // Client should get the finish message and delete itself.
  size_t initial_use_count = cli_rw.use_count();

  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, true);
  client_call_data->Proceed(true);

  size_t final_use_count = cli_rw.use_count();

  // The component that writes stdin silently deletes itself.  The read
  // component is responsible for reporting final status.  Check the shared_ptr
  // reference count to ensure that the client has deleted itself.
  ASSERT_LT(final_use_count, initial_use_count);
}

TEST_F(AsyncEndToEndTest, Client_ExecRead_Test) {
  constexpr int32_t kReturnCode = 1234;

  // Create a service that can accept incoming Exec requests.
  bool operation_status_done = false;
  fuchsia::netemul::guest::CommandListenerPtr listener;
  listener.events().OnTerminated = [&operation_status_done, kReturnCode](zx_status_t status,
                                                                         int32_t ret_code) {
    operation_status_done = true;
    EXPECT_OK(status);
    EXPECT_EQ(ret_code, kReturnCode);
  };
  std::unique_ptr<ListenerInterface> listener_interface =
      std::make_unique<ListenerInterface>(listener.NewRequest());

  grpc::ServerContext srv_ctx;
  std::shared_ptr<grpc::ClientContext> cli_ctx = std::make_shared<grpc::ClientContext>();
  grpc::ServerAsyncReaderWriter<ExecResponse, ExecRequest> srv_rw(&srv_ctx);
  std::shared_ptr<grpc::ClientAsyncReaderWriter<ExecRequest, ExecResponse>> cli_rw;

  service_->RequestExec(&srv_ctx, &srv_rw, server_cq_.get(), server_cq_.get(), &srv_ctx);

  // Create components required to perform a client Exec request.
  ExecReadCallData<FakePlatform>* client_call_data;
  cli_rw = stub_->AsyncExec(cli_ctx.get(), client_cq_.get(), cli_ctx.get());

  // Clear the inital event that is generated by the stub creation.  This would
  // normally be handled by the top-level ExecCallData.
  ASSERT_GRPC_CQ_NEXT(client_cq_, cli_ctx.get(), true);

  client_call_data =
      new ExecReadCallData<FakePlatform>(0, 0, cli_ctx, cli_rw, std::move(listener_interface));

  // Server should get the new stub request and immediately finish.
  ASSERT_GRPC_CQ_NEXT(server_cq_, &srv_ctx, true);

  ExecResponse exec_response;
  exec_response.clear_std_out();
  exec_response.clear_std_err();
  exec_response.set_ret_code(kReturnCode);
  srv_rw.WriteAndFinish(exec_response, grpc::WriteOptions(), grpc::Status::OK, &exec_response);
  ASSERT_GRPC_CQ_NEXT(server_cq_, &exec_response, true);

  // Client should get the message.
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, true);
  client_call_data->Proceed(true);

  // Client should get the finish message.
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, false);
  client_call_data->Proceed(false);

  // Client should run the callback and clean up.
  ASSERT_GRPC_CQ_NEXT(client_cq_, client_call_data, true);
  client_call_data->Proceed(true);

  // The client sets the operation status in the callback.
  RunLoopUntil([&operation_status_done]() { return operation_status_done; });
}
