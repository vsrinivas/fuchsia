// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "server-manager.h"

#include <assert.h>

#include <utility>

#include <ddk/debug.h>

ServerManager::ServerManager() = default;

ServerManager::~ServerManager() { CloseFifoServer(); }

bool ServerManager::IsFifoServerRunning() {
  switch (GetState()) {
    case ThreadState::Running:
      return true;
    case ThreadState::Joinable:
      // Joining the thread here is somewhat arbitrary -- as opposed to joining in
      // |StartServer()| -- but it lets us avoid a second atomic load.
      JoinServer();
      break;
    case ThreadState::None:
      break;
  }
  return false;
}

zx_status_t ServerManager::StartServer(ddk::BlockProtocolClient* protocol, zx::fifo* out_fifo) {
  if (IsFifoServerRunning()) {
    return ZX_ERR_ALREADY_BOUND;
  }
  ZX_DEBUG_ASSERT(server_ == nullptr);
  BlockServer* server;
  fzl::fifo<block_fifo_request_t, block_fifo_response_t> fifo;
  zx_status_t status = BlockServer::Create(protocol, &fifo, &server);
  if (status != ZX_OK) {
    return status;
  }
  server_ = server;
  SetState(ThreadState::Running);
  if (thrd_create(&thread_, &RunServer, this) != thrd_success) {
    FreeServer();
    return ZX_ERR_NO_MEMORY;
  }
  *out_fifo = zx::fifo(fifo.release());
  return ZX_OK;
}

zx_status_t ServerManager::CloseFifoServer() {
  switch (GetState()) {
    case ThreadState::Running:
      server_->ShutDown();
      JoinServer();
      break;
    case ThreadState::Joinable:
      zxlogf(ERROR, "block: Joining un-closed FIFO server\n");
      JoinServer();
      break;
    case ThreadState::None:
      break;
  }
  return ZX_OK;
}

zx_status_t ServerManager::AttachVmo(zx::vmo vmo, vmoid_t* out_vmoid) {
  if (server_ == nullptr) {
    return ZX_ERR_BAD_STATE;
  }
  return server_->AttachVmo(std::move(vmo), out_vmoid);
}

void ServerManager::JoinServer() {
  thrd_join(thread_, nullptr);
  FreeServer();
}

void ServerManager::FreeServer() {
  SetState(ThreadState::None);
  delete server_;
  server_ = nullptr;
}

int ServerManager::RunServer(void* arg) {
  ServerManager* manager = reinterpret_cast<ServerManager*>(arg);

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
