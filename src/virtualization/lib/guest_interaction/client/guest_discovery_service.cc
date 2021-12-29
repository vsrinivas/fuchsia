// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/lib/guest_interaction/client/guest_discovery_service.h"

GuestDiscoveryServiceImpl::GuestDiscoveryServiceImpl(async_dispatcher_t* dispatcher)
    : context_(sys::ServiceDirectory::CreateFromNamespace(), dispatcher), executor_(dispatcher) {
  context_.svc()->Connect(manager_.NewRequest(dispatcher));
  manager_.set_error_handler([this](zx_status_t status) {
    std::lock_guard lock(lock_);
    for (auto& completer : manager_completers_) {
      completer->complete_error(status);
    }
  });
  context_.outgoing()->AddPublicService(bindings_.GetHandler(this, dispatcher));
  context_.outgoing()->ServeFromStartupInfo(dispatcher);
}

void GuestDiscoveryServiceImpl::GetGuest(
    fidl::StringPtr realm_name, std::string guest_name,
    fidl::InterfaceRequest<fuchsia::netemul::guest::GuestInteraction> request) {
  // Find the realm ID and guest instance ID associated with the caller's labels.
  fpromise::bridge<GuestInfo, zx_status_t> bridge;
  std::shared_ptr completer =
      std::make_shared<decltype(bridge.completer)>(std::move(bridge.completer));
  {
    std::lock_guard lock(lock_);
    manager_completers_.insert(completer);
  }
  manager_->List([completer,
                  realm_name = realm_name.value_or(fuchsia::netemul::guest::DEFAULT_REALM),
                  guest_name = std::move(guest_name)](
                     const std::vector<fuchsia::virtualization::EnvironmentInfo>& realm_infos) {
    for (const auto& realm_info : realm_infos) {
      if (realm_info.label == realm_name) {
        for (const auto& instance_info : realm_info.instances) {
          if (instance_info.label == guest_name) {
            completer->complete_ok({
                .realm_id = realm_info.id,
                .guest_cid = instance_info.cid,
            });
            return;
          }
        }
      }
    }
    completer->complete_error(ZX_ERR_NOT_FOUND);
  });
  fpromise::promise<> task =
      bridge.consumer.promise()
          .inspect([this, completer](const fpromise::result<GuestInfo, zx_status_t>&) {
            std::lock_guard lock(lock_);
            manager_completers_.erase(completer);
          })
          .and_then([this](const GuestInfo& guest_info)
                        -> fpromise::promise<FuchsiaGuestInteractionService*, zx_status_t> {
            // If this is not the first time this guest has been requested, add a binding to the
            // existing interaction service.
            {
              auto gis = guests_.find(guest_info);
              if (gis != guests_.end()) {
                return fpromise::make_result_promise<FuchsiaGuestInteractionService*, zx_status_t>(
                    fpromise::ok(&gis->second));
              }
            }

            zx::socket socket, remote_socket;
            zx_status_t status = zx::socket::create(ZX_SOCKET_STREAM, &socket, &remote_socket);
            if (status != ZX_OK) {
              return fpromise::make_result_promise<FuchsiaGuestInteractionService*, zx_status_t>(
                  fpromise::error(status));
            }

            // If this is the first time that the requested guest has been discovered, connect to
            // the guest's vsock and start a new GuestInteractionService for it.
            fuchsia::virtualization::RealmSyncPtr realm;
            manager_->Connect(guest_info.realm_id, realm.NewRequest());

            fpromise::bridge<FuchsiaGuestInteractionService*, zx_status_t> bridge;
            std::shared_ptr completer =
                std::make_shared<decltype(bridge.completer)>(std::move(bridge.completer));

            fuchsia::virtualization::HostVsockEndpointPtr ep;
            ep.set_error_handler(
                [completer](zx_status_t status) { completer->complete_error(status); });
            realm->GetHostVsockEndpoint(ep.NewRequest(executor_.dispatcher()));

            ep->Connect(guest_info.guest_cid, GUEST_INTERACTION_PORT, std::move(remote_socket),
                        [this, socket = std::move(socket), guest_info,
                         completer](zx_status_t status) mutable {
                          if (status != ZX_OK) {
                            completer->complete_error(status);
                            return;
                          }
                          auto [gis, created] = guests_.try_emplace(guest_info, std::move(socket),
                                                                    executor_.dispatcher());
                          // NB: if !created then we lost the race; that's fine, all connections are
                          // equivalent.
                          completer->complete_ok(&gis->second);
                        });
            return bridge.consumer.promise().inspect(
                [ep = std::move(ep)](
                    const fpromise::result<FuchsiaGuestInteractionService*, zx_status_t>&) {});
          })
          .and_then(
              [request = std::move(request)](FuchsiaGuestInteractionService*& service) mutable {
                service->AddBinding(std::move(request));
              })
          .or_else([](const zx_status_t& status) {
            FX_PLOGS(ERROR, status) << "Guest connection failed";
          })
          .wrap_with(scope_);
  executor_.schedule_task(std::move(task));
}
