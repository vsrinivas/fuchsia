// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/lib/guest_interaction/client/guest_interaction_service.h"

#include <lib/async/cpp/task.h>

#include "src/virtualization/lib/grpc/fdio_util.h"
#include "src/virtualization/lib/guest_interaction/common.h"

FuchsiaGuestInteractionService::FuchsiaGuestInteractionService(zx::socket socket,
                                                               async_dispatcher_t* dispatcher)
    : client_([&socket]() {
        fbl::unique_fd fd;
        zx_status_t status = fdio_fd_create(socket.release(), fd.reset_and_get_address());
        if (status != ZX_OK) {
          FX_PLOGS(FATAL, status) << "fdio_fd_create";
        }
        int err = SetNonBlocking(fd);
        if (err != 0) {
          FX_LOGS(FATAL) << "SetNonBlocking: " << strerror(err);
        }
        return fd.release();
      }()),
      dispatcher_(dispatcher) {
  int ret = client_.Start(guest_interaction_service_thread_);
  ZX_ASSERT_MSG(ret == thrd_success, "thread creation failed: %d", ret);
}

FuchsiaGuestInteractionService::~FuchsiaGuestInteractionService() {
  client_.Stop();
  int32_t ret_code;
  int ret = thrd_join(guest_interaction_service_thread_, &ret_code);
  ZX_ASSERT_MSG(ret == thrd_success, "thread joining failed: %d", ret);
  ZX_ASSERT_MSG(ret_code == 0, "thread exited non-zero: %d", ret_code);
}

void FuchsiaGuestInteractionService::PutFile(fidl::InterfaceHandle<fuchsia::io::File> local_file,
                                             std::string remote_path,
                                             fit::function<void(zx_status_t)> callback) {
  client_.Put(
      std::move(local_file), remote_path,
      [dispatcher = dispatcher_, callback = std::move(callback)](zx_status_t status) mutable {
        async::PostTask(dispatcher,
                        [callback = std::move(callback), status]() { callback(status); });
      });
}

void FuchsiaGuestInteractionService::GetFile(std::string remote_path,
                                             fidl::InterfaceHandle<fuchsia::io::File> local_file,
                                             fit::function<void(zx_status_t)> callback) {
  client_.Get(
      remote_path, std::move(local_file),
      [dispatcher = dispatcher_, callback = std::move(callback)](zx_status_t status) mutable {
        async::PostTask(dispatcher,
                        [callback = std::move(callback), status]() { callback(status); });
      });
}

void FuchsiaGuestInteractionService::ExecuteCommand(
    std::string command, std::vector<fuchsia::netemul::guest::EnvironmentVariable> env,
    zx::socket std_in, zx::socket std_out, zx::socket std_err,
    fidl::InterfaceRequest<fuchsia::netemul::guest::CommandListener> req) {
  std::map<std::string, std::string> env_map;
  for (const auto& env_var : env) {
    env_map[env_var.key] = env_var.value;
  }

  client_.Exec(command, env_map, std::move(std_in), std::move(std_out), std::move(std_err),
               std::move(req), dispatcher_);
}

void FuchsiaGuestInteractionService::AddBinding(
    fidl::InterfaceRequest<fuchsia::netemul::guest::GuestInteraction> request) {
  bindings_.AddBinding(this, std::move(request), dispatcher_);
}
