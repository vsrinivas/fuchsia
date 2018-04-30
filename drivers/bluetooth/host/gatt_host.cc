// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gatt_host.h"

#include "fidl/gatt_client_server.h"
#include "fidl/gatt_server_server.h"

using namespace btlib;

namespace bthost {

// static
fbl::RefPtr<GattHost> GattHost::Create(std::string thrd_name) {
  return AdoptRef(new GattHost(std::move(thrd_name)));
}

GattHost::GattHost(std::string thrd_name)
    : btlib::common::TaskDomain<GattHost>(this, std::move(thrd_name)),
      weak_ptr_factory_(this) {
  // Initialize the profile to operate on our task runner.
  gatt_ = gatt::GATT::Create(dispatcher());
  FXL_DCHECK(gatt_);
}

GattHost::~GattHost() {}

void GattHost::Initialize() {
  // Initialize the profile.
  gatt_->Initialize();
  gatt_->RegisterRemoteServiceWatcher(
      [self = fbl::WrapRefPtr(this)](const auto& peer_id, auto service) {
        std::lock_guard<std::mutex> lock(self->mtx_);
        if (self->alive() && self->remote_service_watcher_) {
          self->remote_service_watcher_(peer_id, service);
        }
      });
}

void GattHost::CloseServers() {
  PostMessage([this] { CloseServersInternal(); });
}

void GattHost::ShutDown() {
  // Stop processing further GATT profile requests.
  gatt_->ShutDown();

  // Clear the remote device callback to prevent further notifications after
  // this call.
  {
    std::lock_guard<std::mutex> lock(mtx_);
    remote_service_watcher_ = {};
  }

  btlib::common::TaskDomain<GattHost>::ScheduleCleanUp();
}

void GattHost::CleanUp() {
  AssertOnDispatcherThread();

  CloseServersInternal();
}

void GattHost::BindGattServer(
    fidl::InterfaceRequest<bluetooth_gatt::Server> request) {
  PostMessage([this, request = std::move(request)]() mutable {
    auto self = weak_ptr_factory_.GetWeakPtr();
    auto server = std::make_unique<GattServerServer>(gatt_, std::move(request));
    server->set_error_handler([self, server = server.get()] {
      if (self) {
        FXL_VLOG(1) << "bt-host: GATT server disconnected";
        self->server_servers_.erase(server);
      }
    });
    server_servers_[server.get()] = std::move(server);
  });
}

void GattHost::BindGattClient(
    Token token, std::string peer_id,
    fidl::InterfaceRequest<bluetooth_gatt::Client> request) {
  PostMessage([this, token, peer_id = std::move(peer_id),
               request = std::move(request)]() mutable {
    if (client_servers_.find(token) != client_servers_.end()) {
      FXL_LOG(WARNING) << "gatt: duplicate Client FIDL server tokens!";

      // The handle owned by |request| will be closed.
      return;
    }

    auto self = weak_ptr_factory_.GetWeakPtr();
    auto server = std::make_unique<GattClientServer>(std::move(peer_id), gatt_,
                                                     std::move(request));
    server->set_error_handler([self, token] {
      if (self) {
        FXL_VLOG(1) << "bt-host: GATT client disconnected";
        self->client_servers_.erase(token);
      }
    });
    client_servers_[token] = std::move(server);
  });
}

void GattHost::UnbindGattClient(Token token) {
  PostMessage([this, token] { client_servers_.erase(token); });
}

void GattHost::SetRemoteServiceWatcher(
    btlib::gatt::GATT::RemoteServiceWatcher callback) {
  std::lock_guard<std::mutex> lock(mtx_);
  remote_service_watcher_ = std::move(callback);
}

void GattHost::CloseServersInternal() {
  AssertOnDispatcherThread();

  // This closes all remaining FIDL channels.
  client_servers_.clear();
  server_servers_.clear();
}

}  // namespace bthost
