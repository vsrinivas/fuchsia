// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_SERVICES_SERVICE_IMPL_MANAGER_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_SERVICES_SERVICE_IMPL_MANAGER_H_

#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>

#include <unordered_map>

namespace mdns {

template <typename TProtocol>
class ServiceImplManager {
 public:
  using Deleter = fit::closure;
  using Creator =
      fit::function<std::unique_ptr<TProtocol>(fidl::InterfaceRequest<TProtocol>, Deleter)>;

  ServiceImplManager(Creator creator) : creator_(std::move(creator)) {}

  virtual ~ServiceImplManager() = default;

  // Adds the managed service as in outgoing public service of |component_context|.
  void AddOutgoingPublicService(sys::ComponentContext* component_context) {
    component_context->outgoing()->AddPublicService<TProtocol>(
        fit::bind_member<&ServiceImplManager<TProtocol>::Connect>(this));
  }

  // Satisfies |request| by calling the creator to create a service implementation instance bound to
  // |request|. If |OnReady| has not yet been called, the creation is deferred until it is. The
  // created service implementation instance is kept alive by this manager until the instance calls
  // the deleter passed to the creator.
  void Connect(fidl::InterfaceRequest<TProtocol> request) {
    size_t id = next_id_++;

    if (ready_) {
      // We're ready, so go ahead and create the implementation.
      impls_by_id_.emplace(
          id, std::move(creator_(std::move(request), [this, id]() { impls_by_id_.erase(id); })));
    } else {
      // We're not ready, so store the connect parameters until we are.
      requests_by_id_.emplace(id, std::move(request));
    }
  }

  // Transitions the manager to ready state, performing all deferred |Connect| operations.
  void OnReady() {
    for (auto& pair : requests_by_id_) {
      size_t id = pair.first;
      impls_by_id_.emplace(id, std::move(creator_(std::move(pair.second),
                                                  [this, id]() { impls_by_id_.erase(id); })));
    }

    requests_by_id_.clear();
    ready_ = true;
  }

 private:
  Creator creator_;
  bool ready_ = false;
  size_t next_id_ = 0;
  std::unordered_map<size_t, std::unique_ptr<TProtocol>> impls_by_id_;
  std::unordered_map<size_t, fidl::InterfaceRequest<TProtocol>> requests_by_id_;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_SERVICES_SERVICE_IMPL_MANAGER_H_
