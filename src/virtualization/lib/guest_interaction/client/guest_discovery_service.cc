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

namespace {
// Helpers from the reference documentation for std::visit<>, to allow
// visit-by-overload of std::variant<>.
template <class... Ts>
struct overloaded : Ts... {
  using Ts::operator()...;
};
// explicit deduction guide (not needed as of C++20).
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;
}  // namespace

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
            auto [it, inserted] = guests_.try_emplace(guest_info);
            std::variant<GuestCompleters, FuchsiaGuestInteractionService>& variant = it->second;
            if (!inserted) {
              // An entry already exists; the connection to the guest is either complete or in
              // progress; if it is complete, return a resolved promise, otherwise store a completer
              // to be resolved when the connection completes.
              return std::visit(
                  overloaded{
                      [](FuchsiaGuestInteractionService& guest)
                          -> fpromise::promise<FuchsiaGuestInteractionService*, zx_status_t> {
                        return fpromise::make_result_promise<FuchsiaGuestInteractionService*,
                                                             zx_status_t>(fpromise::ok(&guest));
                      },
                      [](GuestCompleters& completers)
                          -> fpromise::promise<FuchsiaGuestInteractionService*, zx_status_t> {
                        fpromise::bridge<FuchsiaGuestInteractionService*, zx_status_t> bridge;
                        completers.push_back(std::move(bridge.completer));
                        return bridge.consumer.promise();
                      },
                  },
                  variant);
            }

            // We're the first to request a connection to this guest.
            zx::socket socket, remote_socket;
            if (zx_status_t status = zx::socket::create(ZX_SOCKET_STREAM, &socket, &remote_socket);
                status != ZX_OK) {
              return fpromise::make_result_promise<FuchsiaGuestInteractionService*, zx_status_t>(
                  fpromise::error(status));
            }
            fuchsia::virtualization::RealmSyncPtr realm;
            manager_->Connect(guest_info.realm_id, realm.NewRequest());

            fpromise::bridge<FuchsiaGuestInteractionService*, zx_status_t> bridge;
            std::shared_ptr completer =
                std::make_shared<decltype(bridge.completer)>(std::move(bridge.completer));

            fuchsia::virtualization::HostVsockEndpointPtr ep;
            ep.set_error_handler(
                [completer](zx_status_t status) { completer->complete_error(status); });
            realm->GetHostVsockEndpoint(ep.NewRequest(executor_.dispatcher()));

            auto completers_cb = [this, guest_info, &variant, completer,
                                  socket = std::move(socket)](GuestCompleters completers,
                                                              zx_status_t status) mutable {
              if (status != ZX_OK) {
                // Connecting to the guest failed; notify pending completers and remove the entry to
                // allow retries.
                completer->complete_error(status);
                for (auto& pending : completers) {
                  pending.complete_error(status);
                }
                guests_.erase(guest_info);
              } else {
                // Connecting to the guest succeeded; replace pending completers with the complete
                // connection and notify them.
                FuchsiaGuestInteractionService& guest =
                    variant.emplace<FuchsiaGuestInteractionService>(std::move(socket),
                                                                    executor_.dispatcher());
                completer->complete_ok(&guest);
                for (auto& pending : completers) {
                  pending.complete_ok(&guest);
                }
              }
            };

            ep->Connect(guest_info.guest_cid, GUEST_INTERACTION_PORT, std::move(remote_socket),
                        [completer, completers_cb = std::move(completers_cb),
                         &variant](zx_status_t status) mutable {
                          std::visit(overloaded{
                                         [completer](FuchsiaGuestInteractionService& guest) {
                                           // We completed the connection to the guest and
                                           // discovered an already-existing connection. This should
                                           // never happen.
                                           FX_LOGS(ERROR)
                                               << "existing connection in connection callback";
                                           completer->complete_ok(&guest);
                                         },
                                         [&](GuestCompleters& completers) {
                                           completers_cb(std::move(completers), status);
                                         },
                                     },
                                     variant);
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
