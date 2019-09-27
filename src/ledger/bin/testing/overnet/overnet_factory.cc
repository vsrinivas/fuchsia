// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/testing/overnet/overnet_factory.h"

#include <lib/fidl/cpp/clone.h>
#include <lib/fit/function.h>

#include "lib/async/dispatcher.h"
#include "peridot/lib/convert/convert.h"

namespace ledger {
class OvernetFactory::Holder {
 public:
  Holder(async_dispatcher_t* dispatcher, FakeOvernet::Delegate* delegate,
         fidl::InterfaceRequest<fuchsia::overnet::Overnet> request, uint64_t device_name,
         fit::closure on_disconnect);
  ~Holder();

  void SetOnDiscardable(fit::closure on_discardable);
  bool IsDiscardable() const;

  FakeOvernet* impl();

 private:
  fidl_helpers::BoundInterface<fuchsia::overnet::Overnet, FakeOvernet> interface_;
  fit::closure on_disconnect_;
};

OvernetFactory::Holder::Holder(async_dispatcher_t* dispatcher, FakeOvernet::Delegate* delegate,
                               fidl::InterfaceRequest<fuchsia::overnet::Overnet> request,
                               uint64_t device_id, fit::closure on_disconnect)
    : interface_(std::move(request), dispatcher, device_id, delegate),
      on_disconnect_(std::move(on_disconnect)) {}

OvernetFactory::Holder::~Holder() {
  if (on_disconnect_) {
    auto on_disconnect = std::move(on_disconnect_);
    on_disconnect();
  }
}

void OvernetFactory::Holder::SetOnDiscardable(fit::closure on_discardable) {
  interface_.SetOnDiscardable(std::move(on_discardable));
}

bool OvernetFactory::Holder::IsDiscardable() const { return interface_.IsDiscardable(); }

FakeOvernet* OvernetFactory::Holder::impl() { return interface_.impl(); }

OvernetFactory::OvernetFactory(async_dispatcher_t* dispatcher, bool return_one_host_list)
    : dispatcher_(dispatcher),
      return_one_host_list_(return_one_host_list),
      net_connectors_(dispatcher) {}

OvernetFactory::~OvernetFactory() = default;

void OvernetFactory::AddBinding(uint64_t node_id,
                                fidl::InterfaceRequest<fuchsia::overnet::Overnet> request) {
  net_connectors_.try_emplace(node_id, dispatcher_, this, std::move(request), node_id,
                              [this] { UpdatedHostList(); });
  UpdatedHostList();
}

void OvernetFactory::UpdatedHostList() {
  current_version_++;
  if (pending_device_list_callbacks_.empty()) {
    return;
  }
  std::vector<fuchsia::overnet::protocol::NodeId> device_names;
  for (const auto& holder_pair : net_connectors_) {
    fuchsia::overnet::protocol::NodeId node;
    node.id = holder_pair.first;
    device_names.push_back(std::move(node));
  }
  for (const auto& callback : pending_device_list_callbacks_) {
    callback(current_version_, device_names);
  }
  pending_device_list_callbacks_.clear();
}

void OvernetFactory::ListPeers(
    uint64_t last_version,
    fit::function<void(uint64_t, std::vector<fuchsia::overnet::protocol::NodeId>)> callback) {
  FXL_CHECK(last_version <= current_version_)
      << "Last seen version (" << last_version << ") is more recent than current version ("
      << current_version_ << "). Something is wrong here.";
  if (last_version == current_version_) {
    pending_device_list_callbacks_.push_back(std::move(callback));
    return;
  }
  std::vector<fuchsia::overnet::protocol::NodeId> device_names;
  if (return_one_host_list_ && net_connectors_.size() == 1) {
    callback(current_version_, {});
    return;
  }
  for (const auto& holder_pair : net_connectors_) {
    fuchsia::overnet::protocol::NodeId node;
    node.id = holder_pair.first;
    device_names.push_back(std::move(node));
  }
  callback(current_version_, std::move(device_names));
}

void OvernetFactory::ConnectToService(fuchsia::overnet::protocol::NodeId device_name,
                                      std::string service_name, zx::channel channel) {
  auto it = net_connectors_.find(device_name.id);
  if (it == net_connectors_.end()) {
    return;
  }
  (*it).second.impl()->GetService(std::move(service_name), std::move(channel));
}

}  // namespace ledger
