// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/lib/guest_interaction/client/guest_interaction_service.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <stdlib.h>

#include "src/virtualization/lib/grpc/fdio_util.h"
#include "src/virtualization/lib/guest_interaction/common.h"

static int run_grpc_loop(void* service_to_run) {
  auto gis = static_cast<FuchsiaGuestInteractionService*>(service_to_run);
  gis->Run();
  return 0;
}

FuchsiaGuestInteractionService::FuchsiaGuestInteractionService(zx::socket socket)
    : executor_(async_get_default_dispatcher()) {
  fbl::unique_fd fd;
  zx_status_t status = fdio_fd_create(socket.release(), fd.reset_and_get_address());
  if (status != ZX_OK) {
    abort();
  }
  if (SetNonBlocking(fd) != 0) {
    abort();
  }
  client_ = std::make_unique<ClientImpl<PosixPlatform>>(fd.release());
  thrd_create_with_name(&guest_interaction_service_thread_, run_grpc_loop, this, "gRPC run");
}

FuchsiaGuestInteractionService::~FuchsiaGuestInteractionService() { Stop(); }

fpromise::promise<zx_status_t> FuchsiaGuestInteractionService::InitiatePut(
    zx::channel local_file, const std::string& remote_path) {
  fpromise::bridge<zx_status_t> bridge;
  client_->Put(std::move(local_file), remote_path,
               [completer = std::move(bridge.completer)](zx_status_t status) mutable {
                 completer.complete_ok(status);
               });
  return bridge.consumer.promise();
}

void FuchsiaGuestInteractionService::PutFile(fidl::InterfaceHandle<fuchsia::io::File> local_file,
                                             std::string remote_path,
                                             fit::function<void(zx_status_t)> callback) {
  executor_.schedule_task(
      InitiatePut(local_file.TakeChannel(), remote_path)
          .and_then([callback = std::move(callback),
                     local_file = std::move(local_file)](zx_status_t& status) { callback(status); })
          .wrap_with(scope_));
}

fpromise::promise<zx_status_t> FuchsiaGuestInteractionService::InitiateGet(
    const std::string& remote_path, zx::channel local_file) {
  fpromise::bridge<zx_status_t> bridge;
  client_->Get(remote_path, std::move(local_file),
               [completer = std::move(bridge.completer)](zx_status_t status) mutable {
                 completer.complete_ok(status);
               });
  return bridge.consumer.promise();
}

void FuchsiaGuestInteractionService::GetFile(std::string remote_path,
                                             fidl::InterfaceHandle<fuchsia::io::File> local_file,
                                             fit::function<void(zx_status_t)> callback) {
  executor_.schedule_task(
      InitiateGet(remote_path, local_file.TakeChannel())
          .and_then([callback = std::move(callback),
                     local_file = std::move(local_file)](zx_status_t& status) { callback(status); })
          .wrap_with(scope_));
}

void FuchsiaGuestInteractionService::ExecuteCommand(
    std::string command, std::vector<fuchsia::netemul::guest::EnvironmentVariable> env,
    zx::socket std_in, zx::socket std_out, zx::socket std_err,
    fidl::InterfaceRequest<fuchsia::netemul::guest::CommandListener> req) {
  std::map<std::string, std::string> env_map;
  for (const auto& env_var : env) {
    env_map[env_var.key] = env_var.value;
  }

  client_->Exec(command, env_map, std::move(std_in), std::move(std_out), std::move(std_err),
                std::move(req));
}

void FuchsiaGuestInteractionService::AddBinding(
    fidl::InterfaceRequest<fuchsia::netemul::guest::GuestInteraction> request) {
  bindings_.AddBinding(this, std::move(request));
}

void FuchsiaGuestInteractionService::Run() {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  async_set_default_dispatcher(loop.dispatcher());
  loop.StartThread();
  client_->Run();
  loop.Quit();
}

void FuchsiaGuestInteractionService::Stop() {
  client_->Stop();
  int32_t ret_code;
  thrd_join(guest_interaction_service_thread_, &ret_code);
}
