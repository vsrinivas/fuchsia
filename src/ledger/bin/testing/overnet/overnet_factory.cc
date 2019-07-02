// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/testing/overnet/overnet_factory.h"

#include <lib/fidl/cpp/clone.h>
#include <lib/fit/function.h>

#include "peridot/lib/convert/convert.h"

namespace ledger {
class OvernetFactory::Holder {
 public:
  Holder(FakeOvernet::Delegate* delegate, fidl::InterfaceRequest<fuchsia::overnet::Overnet> request,
         uint64_t device_name, fit::closure on_disconnect);

  void set_on_empty(fit::closure on_empty);

  FakeOvernet* impl();

 private:
  void OnEmpty();

  fidl_helpers::BoundInterface<fuchsia::overnet::Overnet, FakeOvernet> interface_;
  fit::closure on_empty_;
  fit::closure on_disconnect_;
};

OvernetFactory::Holder::Holder(FakeOvernet::Delegate* delegate,
                               fidl::InterfaceRequest<fuchsia::overnet::Overnet> request,
                               uint64_t device_id, fit::closure on_disconnect)
    : interface_(std::move(request), device_id, delegate),
      on_disconnect_(std::move(on_disconnect)) {
  interface_.set_on_empty([this] { OnEmpty(); });
}

void OvernetFactory::Holder::set_on_empty(fit::closure on_empty) {
  on_empty_ = std::move(on_empty);
}

FakeOvernet* OvernetFactory::Holder::impl() { return interface_.impl(); }

void OvernetFactory::Holder::OnEmpty() {
  // We need to deregister ourselves from the list of active devices (call
  // |on_empty_|) before updating the pending host list callbacks (call
  // |on_disconnect_|). As |on_empty_| destroys |this|, we move |on_disconnect_|
  // locally to be able to call it later.
  auto on_disconnect = std::move(on_disconnect_);
  if (on_empty_) {
    on_empty_();
  }
  if (on_disconnect) {
    on_disconnect();
  }
}

OvernetFactory::OvernetFactory() {}

OvernetFactory::~OvernetFactory() {}

void OvernetFactory::AddBinding(uint64_t node_id,
                                fidl::InterfaceRequest<fuchsia::overnet::Overnet> request) {
  net_connectors_.emplace(
      std::piecewise_construct, std::forward_as_tuple(node_id),
      std::forward_as_tuple(this, std::move(request), node_id, [this] { UpdatedHostList(); }));
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
