// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "manager.h"

#include <assert.h>
#include <lib/ddk/debug.h>
#include <zircon/threads.h>

#include <utility>

Manager::Manager() = default;

Manager::~Manager() { CloseFifoServer(); }

bool Manager::IsFifoServerRunning() {
  {
    std::scoped_lock lock(mutex_);
    switch (state_) {
      case ThreadState::Running:
        // See if the server is about to terminate.
        if (!server_->WillTerminate())
          return true;
        // It is, so wait.
        while (state_ != ThreadState::Joinable)
          condition_.wait(mutex_);
        break;
      case ThreadState::Joinable:
        break;
      case ThreadState::None:
        return false;
    }
  }
  // Joining the thread here is somewhat arbitrary -- as opposed to joining in |StartServer()|.
  JoinServer();
  return false;
}

zx_status_t Manager::StartServer(zx_device_t* device, ddk::BlockProtocolClient* protocol,
                                 zx::fifo* out_fifo) {
  if (IsFifoServerRunning()) {
    return ZX_ERR_ALREADY_BOUND;
  }
  ZX_DEBUG_ASSERT(server_ == nullptr);
  std::unique_ptr<Server> server;
  fzl::fifo<block_fifo_request_t, block_fifo_response_t> fifo;
  zx_status_t status = Server::Create(protocol, &fifo, &server);
  if (status != ZX_OK) {
    return status;
  }
  server_ = std::move(server);
  SetState(ThreadState::Running);
  if (thrd_create_with_name(&thread_, &RunServer, this, "block_server") != thrd_success) {
    FreeServer();
    return ZX_ERR_NO_MEMORY;
  }

  // Set a scheduling deadline profile for the block_server thread.
  // This is required in order to service the blobfs-pager-thread, which is on a deadline profile.
  // This will no longer be needed once we have the ability to propagate deadlines. Until then, we
  // need to set deadline profiles for all threads that the blobfs-pager-thread interacts with in
  // order to service page requests.
  //
  // Also note that this will apply to block_server threads spawned to service each block client
  // (in the typical case, we have two - blobfs and minfs). The capacity of 1ms is chosen so as to
  // accommodate most cases without throttling the thread. The desired capacity was 50us, but some
  // tests that use a large ramdisk require a larger capacity. In the average case though on a real
  // device, the block_server thread runs for less than 50us. 1ms provides us with a generous
  // leeway, without hurting performance in the typical case - a thread is not penalized for not
  // using its full capacity.
  //
  // TODO(fxbug.dev/40858): Migrate to the role-based API when available, instead of hard
  // coding parameters.
  const zx_duration_t capacity = ZX_MSEC(1);
  const zx_duration_t deadline = ZX_MSEC(2);
  const zx_duration_t period = deadline;

  zx_handle_t profile = ZX_HANDLE_INVALID;
  status = device_get_deadline_profile(device, capacity, deadline, period,
                                       "driver_host:pdev:05:00:f:block_server", &profile);
  if (status != ZX_OK) {
    zxlogf(WARNING, "block: Failed to get deadline profile: %d\n", status);
  } else {
    const zx_handle_t thread_handle = thrd_get_zx_handle(thread_);
    status = zx_object_set_profile(thread_handle, profile, 0);
    if (status != ZX_OK) {
      zxlogf(WARNING, "block: Failed to set deadline profile: %d\n", status);
    }
    zx_handle_close(profile);
  }

  *out_fifo = zx::fifo(fifo.release());
  return ZX_OK;
}

zx_status_t Manager::CloseFifoServer() {
  switch (GetState()) {
    case ThreadState::Running:
      server_->Shutdown();
      JoinServer();
      break;
    case ThreadState::Joinable:
      zxlogf(ERROR, "block: Joining un-closed FIFO server");
      JoinServer();
      break;
    case ThreadState::None:
      break;
  }
  return ZX_OK;
}

zx_status_t Manager::AttachVmo(zx::vmo vmo, vmoid_t* out_vmoid) {
  if (server_ == nullptr) {
    return ZX_ERR_BAD_STATE;
  }
  return server_->AttachVmo(std::move(vmo), out_vmoid);
}

void Manager::JoinServer() {
  thrd_join(thread_, nullptr);
  FreeServer();
}

void Manager::FreeServer() {
  SetState(ThreadState::None);
  server_.reset();
}

int Manager::RunServer(void* arg) {
  Manager* manager = reinterpret_cast<Manager*>(arg);

  // The completion of "thrd_create" synchronizes-with the beginning of this thread, so
  // we may assume that "manager->server_" is available for our usage.
  //
  // The "manager->server_" pointer shall not be modified by this thread.
  //
  // The "manager->server_" pointer will only be nullified after thrd_join, because join
  // synchronizes-with the completion of this thread.
  ZX_DEBUG_ASSERT(manager->server_);
  manager->server_->Serve();
  manager->SetState(ThreadState::Joinable);
  return 0;
}
