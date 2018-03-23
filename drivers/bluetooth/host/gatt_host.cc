// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gatt_host.h"

#include "fidl/gatt_server_server.h"

#include "lib/fxl/functional/make_copyable.h"

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
  gatt_ = gatt::GATT::Create(task_runner());
  FXL_DCHECK(gatt_);
}

void GattHost::Initialize() {
  // Initialize the profile.
  gatt_->Initialize();
}

void GattHost::CloseServers() {
  PostMessage([this] { servers_.clear(); });
}

void GattHost::ShutDown() {
  // Stop processing further GATT profile requests.
  gatt_->ShutDown();

  btlib::common::TaskDomain<GattHost>::ScheduleCleanUp();
}

void GattHost::CleanUp() {
  FXL_DCHECK(task_runner()->RunsTasksOnCurrentThread());

  // This closes all remaining FIDL channels.
  servers_.clear();
}

void GattHost::BindGattServer(
    fidl::InterfaceRequest<bluetooth_gatt::Server> request) {
  // TODO(armansito): Stop using MakeCopyable! (NET-425)
  PostMessage(fxl::MakeCopyable([this, request = std::move(request)]() mutable {
    AddServer(std::make_unique<GattServerServer>(gatt_, std::move(request)));
  }));
}

void GattHost::AddServer(std::unique_ptr<Server> server) {
  FXL_DCHECK(task_runner()->RunsTasksOnCurrentThread());
  FXL_DCHECK(server);

  auto self = weak_ptr_factory_.GetWeakPtr();
  server->set_error_handler([self, server = server.get()] {
    if (self) {
      self->servers_.erase(server);
    }
  });

  servers_[server.get()] = std::move(server);
}

}  // namespace bthost
