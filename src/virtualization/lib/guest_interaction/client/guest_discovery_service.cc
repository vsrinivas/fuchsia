// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "guest_discovery_service.h"

static bool find_guest_ids(const std::string& realm_name, const std::string& guest_name,
                           const std::vector<fuchsia::virtualization::EnvironmentInfo>& realm_infos,
                           GuestInfo* guest_info) {
  for (const auto& realm_info : realm_infos) {
    if (realm_info.label == realm_name) {
      for (const auto& instance_info : realm_info.instances) {
        if (instance_info.label == guest_name) {
          guest_info->realm_id = realm_info.id;
          guest_info->guest_cid = instance_info.cid;
          return true;
        }
      }
    }
  }
  return false;
}

GuestDiscoveryServiceImpl::GuestDiscoveryServiceImpl()
    : context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()),
      executor_(async_get_default_dispatcher()) {
  context_->svc()->Connect(manager_.NewRequest());
  context_->outgoing()->AddPublicService(bindings_.GetHandler(this));
}

void GuestDiscoveryServiceImpl::GetGuest(
    fidl::StringPtr realm_name, std::string guest_name,
    fidl::InterfaceRequest<fuchsia::netemul::guest::GuestInteraction> request) {
  executor_.schedule_task(
      fpromise::make_promise(
          [this, realm_name = std::move(realm_name), guest_name]() -> fpromise::promise<GuestInfo> {
            // Find the realm ID and guest instance ID associated with the caller's labels.
            fpromise::bridge<GuestInfo, void> bridge;
            manager_->List(
                [completer = std::move(bridge.completer), realm_name = std::move(realm_name),
                 guest_name](
                    std::vector<fuchsia::virtualization::EnvironmentInfo> realm_infos) mutable {
                  GuestInfo guest_info;
                  if (!find_guest_ids(realm_name.value_or(fuchsia::netemul::guest::DEFAULT_REALM),
                                      guest_name, realm_infos, &guest_info)) {
                    completer.complete_error();
                    return;
                  }
                  completer.complete_ok(guest_info);
                });
            return bridge.consumer.promise();
          })
          .and_then([this, request = std::move(request)](
                        const GuestInfo& guest_info) mutable -> fpromise::promise<void> {
            // If this is not the first time this guest has been requested, add a binding to the
            // existing interaction service.
            auto gis = guests_.find(guest_info);
            if (gis != guests_.end()) {
              gis->second->AddBinding(std::move(request));
              return fpromise::make_ok_promise();
            }

            // If this is the first time that the requested guest has been discovered, connect to
            // the guest's vsock and start a new GuestInteractionService for it.
            fuchsia::virtualization::RealmPtr realm;
            manager_->Connect(guest_info.realm_id, realm.NewRequest());

            fuchsia::virtualization::HostVsockEndpointPtr ep;
            realm->GetHostVsockEndpoint(ep.NewRequest());

            zx::socket socket, remote_socket;
            zx_status_t status = zx::socket::create(ZX_SOCKET_STREAM, &socket, &remote_socket);

            if (status != ZX_OK) {
              return fpromise::make_error_promise();
            }

            fpromise::bridge<void> bridge;
            ep->Connect(guest_info.guest_cid, GUEST_INTERACTION_PORT, std::move(remote_socket),
                        [this, socket = std::move(socket), guest_info, request = std::move(request),
                         completer = std::move(bridge.completer),
                         ep = std::move(ep)](zx_status_t status) mutable {
                          if (status != ZX_OK) {
                            completer.complete_error();
                            return;
                          }
                          guests_.emplace(
                              guest_info,
                              std::make_unique<FuchsiaGuestInteractionService>(std::move(socket)));
                          guests_[guest_info]->AddBinding(std::move(request));
                          completer.complete_ok();
                        });
            return bridge.consumer.promise();
          }));
}
