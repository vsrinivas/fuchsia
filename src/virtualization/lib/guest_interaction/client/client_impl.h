// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_CLIENT_CLIENT_IMPL_H_
#define SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_CLIENT_CLIENT_IMPL_H_

#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fit/function.h>
#include <zircon/status.h>

#include <filesystem>

#include "src/virtualization/lib/grpc/fdio_util.h"
#include "src/virtualization/lib/guest_interaction/client/client_operation_state.h"
#include "src/virtualization/lib/guest_interaction/platform_interface/platform_interface.h"
#include "src/virtualization/lib/guest_interaction/proto/guest_interaction.grpc.pb.h"

template <class T>
class ClientImpl {
 public:
  // The gRPC channel internals take responsibility for closing the supplied vsock_fd.
  explicit ClientImpl(int vsock_fd)
      : stub_(GuestInteractionService::NewStub(
            grpc::CreateInsecureChannelFromFd("vsock", vsock_fd))) {}

  void Run() {
    running_ = true;
    run();
  }

  [[nodiscard]] int Start(thrd_t& thread) {
    running_ = true;

    return thrd_create_with_name(
        &thread,
        [](void* ctx) {
          ClientImpl& self = *static_cast<ClientImpl*>(ctx);
          self.run();
          return 0;
        },
        this, "ClientImpl");
  }

  void Stop() { running_ = false; }

  void Get(const std::string& source, fidl::InterfaceHandle<fuchsia::io::File> local_file,
           TransferCallback callback) {
    int32_t fd;
    zx_status_t status = fdio_fd_create(local_file.TakeChannel().release(), &fd);
    if (status != ZX_OK) {
      callback(OperationStatus::CLIENT_CREATE_FILE_FAILURE);
      return;
    }

    GetRequest get_request;
    get_request.set_source(source);

    GetCallData<T>* call_data = new GetCallData<T>(fd, std::move(callback));
    call_data->reader_ = stub_->PrepareAsyncGet(&(call_data->ctx_), get_request, &cq_);
    call_data->reader_->StartCall(call_data);
  }

  void Put(fidl::InterfaceHandle<fuchsia::io::File> local_file, const std::string& destination,
           TransferCallback callback) {
    int32_t fd;
    zx_status_t status = fdio_fd_create(local_file.TakeChannel().release(), &fd);
    if (status != ZX_OK) {
      callback(OperationStatus::CLIENT_FILE_READ_FAILURE);
      return;
    }

    PutCallData<T>* call_data = new PutCallData<T>(fd, destination, std::move(callback));
    call_data->writer_ = stub_->PrepareAsyncPut(&(call_data->ctx_), &(call_data->response_), &cq_);
    call_data->writer_->StartCall(call_data);
  }

  void Exec(const std::string& command, const std::map<std::string, std::string>& env_vars,
            zx::socket std_in, zx::socket std_out, zx::socket std_err,
            fidl::InterfaceRequest<fuchsia::netemul::guest::CommandListener> req,
            async_dispatcher_t* dispatcher) {
    // Convert the provided zx::sockets into FDs.
    auto convert = [](zx::socket socket) {
      fbl::unique_fd fd;
      if (socket.is_valid()) {
        zx_status_t status = fdio_fd_create(socket.release(), fd.reset_and_get_address());
        if (status != ZX_OK) {
          FX_LOGS(FATAL) << "Failed to create file descriptor: " << zx_status_get_string(status);
        }
      } else {
        *fd.reset_and_get_address() = fdio_fd_create_null();
      }
      int result = SetNonBlocking(fd);
      if (result != 0) {
        FX_LOGS(FATAL) << "Failed to set non-blocking: " << strerror(result);
      }
      return std::move(fd);
    };
    fbl::unique_fd stdin_fd = convert(std::move(std_in));
    fbl::unique_fd stdout_fd = convert(std::move(std_out));
    fbl::unique_fd stderr_fd = convert(std::move(std_err));

    std::unique_ptr<ListenerInterface> listener =
        std::make_unique<ListenerInterface>(std::move(req), dispatcher);

    ExecCallData<T>* call_data =
        new ExecCallData<T>(command, env_vars, stdin_fd.release(), stdout_fd.release(),
                            stderr_fd.release(), std::move(listener));
    call_data->rw_ = stub_->PrepareAsyncExec(call_data->ctx_.get(), &cq_);
    call_data->rw_->StartCall(call_data);
  }

 private:
  void run() {
    void* tag;
    bool ok;
    constexpr gpr_timespec deadline = {
        .tv_nsec = 100 * 1000,  // 100ms.
        .clock_type = GPR_TIMESPAN,
    };

    while (running_) {
      switch (cq_.AsyncNext(&tag, &ok, deadline)) {
        case grpc::CompletionQueue::SHUTDOWN:
          FX_LOGS(FATAL) << "completion queue shutdown";
          break;
        case grpc::CompletionQueue::GOT_EVENT:
          static_cast<CallData*>(tag)->Proceed(ok);
          break;
        case grpc::CompletionQueue::TIMEOUT:
          break;
      };
    }
  }

  grpc::CompletionQueue cq_;
  const std::unique_ptr<GuestInteractionService::Stub> stub_;

  std::atomic<bool> running_;
};

#endif  // SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_CLIENT_CLIENT_IMPL_H_
