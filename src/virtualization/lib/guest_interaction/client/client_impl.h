// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_CLIENT_CLIENT_IMPL_H_
#define SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_CLIENT_CLIENT_IMPL_H_

#include <lib/fdio/fd.h>
#include <zircon/system/ulib/fit/include/lib/fit/function.h>

#include <filesystem>

#include "src/virtualization/lib/grpc/fdio_util.h"
#include "src/virtualization/lib/guest_interaction/client/client_operation_state.h"
#include "src/virtualization/lib/guest_interaction/platform_interface/platform_interface.h"
#include "src/virtualization/lib/guest_interaction/proto/guest_interaction.grpc.pb.h"

template <class T>
class ClientImpl {
 public:
  explicit ClientImpl(uint32_t vsock_fd);
  void Get(const std::string& source, zx::channel channel, TransferCallback callback);
  void Put(zx::channel source_channel, const std::string& destination, TransferCallback callback);
  void Exec(const std::string& command, const std::map<std::string, std::string>& env_vars,
            zx::socket std_in, zx::socket std_out, zx::socket std_err, ExecCallback callback);
  void Run();
  void Stop();

 private:
  grpc::CompletionQueue cq_;
  std::unique_ptr<GuestInteractionService::Stub> stub_;

  bool should_run_;
  T platform_interface_;
};

// The gRPC channel internals take responsibility for closing the supplied vsock_fd.
template <class T>
ClientImpl<T>::ClientImpl(uint32_t vsock_fd) : should_run_(true) {
  stub_ = GuestInteractionService::NewStub(grpc::CreateInsecureChannelFromFd("vsock", vsock_fd));
}

template <class T>
void ClientImpl<T>::Run() {
  void* tag;
  bool ok;
  gpr_timespec wait_time = {};

  should_run_ = true;

  while (should_run_) {
    grpc::CompletionQueue::NextStatus status = cq_.AsyncNext(&tag, &ok, wait_time);
    if (status == grpc::CompletionQueue::GOT_EVENT) {
      static_cast<CallData*>(tag)->Proceed(ok);
    }
  }
}

template <class T>
void ClientImpl<T>::Stop() {
  should_run_ = false;
}

template <class T>
void ClientImpl<T>::Get(const std::string& source, zx::channel channel, TransferCallback callback) {
  int32_t fd;
  zx_status_t fd_create_status = fdio_fd_create(channel.release(), &fd);
  if (fd_create_status != ZX_OK) {
    callback(OperationStatus::CLIENT_CREATE_FILE_FAILURE);
    return;
  }

  GetRequest get_request;
  get_request.set_source(source);

  GetCallData<T>* call_data = new GetCallData<T>(fd, std::move(callback));
  call_data->reader_ = stub_->AsyncGet(&(call_data->ctx_), get_request, &cq_, call_data);
}

template <class T>
void ClientImpl<T>::Put(zx::channel source_channel, const std::string& destination,
                        TransferCallback callback) {
  int32_t fd;
  zx_status_t fd_create_status = fdio_fd_create(source_channel.release(), &fd);
  if (fd_create_status != ZX_OK) {
    callback(OperationStatus::CLIENT_FILE_READ_FAILURE);
    return;
  }

  PutCallData<T>* call_data = new PutCallData<T>(fd, destination, std::move(callback));
  call_data->writer_ =
      stub_->AsyncPut(&(call_data->ctx_), &(call_data->response_), &cq_, call_data);
}

template <class T>
void ClientImpl<T>::Exec(const std::string& command,
                         const std::map<std::string, std::string>& env_vars, zx::socket std_in,
                         zx::socket std_out, zx::socket std_err, ExecCallback callback) {
  // Convert the provided zx::sockets into FD's.
  int32_t stdin_fd = ConvertSocketToNonBlockingFd(std::move(std_in));
  int32_t stdout_fd = ConvertSocketToNonBlockingFd(std::move(std_out));
  int32_t stderr_fd = ConvertSocketToNonBlockingFd(std::move(std_err));

  ExecCallData<T>* call_data =
      new ExecCallData<T>(command, env_vars, stdin_fd, stdout_fd, stderr_fd, std::move(callback));
  call_data->rw_ = stub_->AsyncExec(call_data->ctx_.get(), &cq_, call_data);
}

#endif  // SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_CLIENT_CLIENT_IMPL_H_
