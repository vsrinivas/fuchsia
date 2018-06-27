// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/testing/netconnector/netconnector_factory.h"

#include <lib/fit/function.h>

#include "lib/fidl/cpp/clone.h"
#include "peridot/lib/convert/convert.h"

namespace ledger {
class NetConnectorFactory::Holder {
 public:
  Holder(FakeNetConnector::Delegate* delegate,
         fidl::InterfaceRequest<fuchsia::netconnector::NetConnector> request,
         std::string device_name, fit::closure on_disconnect);

  void set_on_empty(fit::closure on_empty);

  FakeNetConnector* impl();

 private:
  void OnEmpty();

  const std::string device_name_;
  ledger::fidl_helpers::BoundInterface<fuchsia::netconnector::NetConnector,
                                       FakeNetConnector>
      interface_;
  fit::closure on_empty_;
  fit::closure on_disconnect_;
};

NetConnectorFactory::Holder::Holder(
    FakeNetConnector::Delegate* delegate,
    fidl::InterfaceRequest<fuchsia::netconnector::NetConnector> request,
    std::string device_name, fit::closure on_disconnect)
    : device_name_(std::move(device_name)),
      interface_(std::move(request), delegate),
      on_disconnect_(std::move(on_disconnect)) {
  interface_.set_on_empty([this] { OnEmpty(); });
}

void NetConnectorFactory::Holder::set_on_empty(fit::closure on_empty) {
  on_empty_ = std::move(on_empty);
}

FakeNetConnector* NetConnectorFactory::Holder::impl() {
  return interface_.impl();
}

void NetConnectorFactory::Holder::OnEmpty() {
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

NetConnectorFactory::NetConnectorFactory() {}

NetConnectorFactory::~NetConnectorFactory() {}

void NetConnectorFactory::AddBinding(
    std::string host_name,
    fidl::InterfaceRequest<fuchsia::netconnector::NetConnector> request) {
  net_connectors_.emplace(
      std::piecewise_construct, std::forward_as_tuple(host_name),
      std::forward_as_tuple(this, std::move(request), host_name,
                            [this] { UpdatedHostList(); }));
  UpdatedHostList();
}

void NetConnectorFactory::UpdatedHostList() {
  current_version_++;
  if (pending_device_list_callbacks_.empty()) {
    return;
  }
  fidl::VectorPtr<fidl::StringPtr> device_names;
  for (const auto& holder_pair : net_connectors_) {
    device_names.push_back(holder_pair.first);
  }
  for (const auto& callback : pending_device_list_callbacks_) {
    callback(current_version_, fidl::Clone(device_names));
  }
  pending_device_list_callbacks_.clear();
}

void NetConnectorFactory::GetDevicesNames(
    uint64_t last_version,
    fit::function<void(uint64_t, fidl::VectorPtr<fidl::StringPtr>)> callback) {
  FXL_CHECK(last_version <= current_version_)
      << "Last seen version (" << last_version
      << ") is more recent than current version (" << current_version_
      << "). Something is wrong here.";
  if (last_version == current_version_) {
    pending_device_list_callbacks_.push_back(std::move(callback));
    return;
  }
  fidl::VectorPtr<fidl::StringPtr> device_names;
  for (const auto& holder_pair : net_connectors_) {
    device_names.push_back(holder_pair.first);
  }
  callback(current_version_, std::move(device_names));
}

void NetConnectorFactory::ConnectToServiceProvider(
    std::string device_name,
    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> request) {
  auto it = net_connectors_.find(device_name);
  if (it == net_connectors_.end()) {
    return;
  }
  (*it).second.impl()->ConnectToServiceProvider(std::move(request));
}

}  // namespace ledger
