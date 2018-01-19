// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/testing/netconnector/netconnector_factory.h"
#include "peridot/lib/convert/convert.h"

namespace ledger {
class NetConnectorFactory::Holder {
 public:
  Holder(FakeNetConnector::Delegate* delegate,
         fidl::InterfaceRequest<netconnector::NetConnector> request,
         std::string device_name,
         fxl::Closure on_disconnect);

  void set_on_empty(fxl::Closure on_empty);

  FakeNetConnector* impl();

 private:
  void OnEmpty();

  const std::string device_name_;
  ledger::fidl_helpers::BoundInterface<netconnector::NetConnector,
                                       FakeNetConnector>
      interface_;
  fxl::Closure on_empty_;
  fxl::Closure on_disconnect_;
};

NetConnectorFactory::Holder::Holder(
    FakeNetConnector::Delegate* delegate,
    fidl::InterfaceRequest<netconnector::NetConnector> request,
    std::string device_name,
    fxl::Closure on_disconnect)
    : device_name_(device_name),
      interface_(std::move(request), delegate),
      on_disconnect_(std::move(on_disconnect)) {
  interface_.set_on_empty([this] { OnEmpty(); });
}

void NetConnectorFactory::Holder::set_on_empty(fxl::Closure on_empty) {
  on_empty_ = std::move(on_empty);
}

FakeNetConnector* NetConnectorFactory::Holder::impl() {
  return interface_.impl();
}

void NetConnectorFactory::Holder::OnEmpty() {
  if (on_disconnect_) {
    on_disconnect_();
  }
  if (on_empty_) {
    on_empty_();
  }
}

NetConnectorFactory::NetConnectorFactory() {}

NetConnectorFactory::~NetConnectorFactory() {}

void NetConnectorFactory::AddBinding(
    std::string host_name,
    fidl::InterfaceRequest<netconnector::NetConnector> request) {
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
  fidl::Array<fidl::String> device_names;
  for (const auto& holder_pair : net_connectors_) {
    device_names.push_back(holder_pair.first);
  }
  for (auto callback : pending_device_list_callbacks_) {
    callback(current_version_, device_names.Clone());
  }
  pending_device_list_callbacks_.clear();
}

void NetConnectorFactory::GetDevicesNames(
    uint64_t last_version,
    std::function<void(uint64_t, fidl::Array<fidl::String>)> callback) {
  FXL_CHECK(last_version <= current_version_)
      << "Last seen version (" << last_version
      << ") is more recent than current version (" << current_version_
      << "). Something is wrong here.";
  if (last_version == current_version_) {
    pending_device_list_callbacks_.push_back(std::move(callback));
    return;
  }
  fidl::Array<fidl::String> device_names;
  for (const auto& holder_pair : net_connectors_) {
    device_names.push_back(holder_pair.first);
  }
  callback(current_version_, std::move(device_names));
  return;
}

void NetConnectorFactory::ConnectToServiceProvider(
    std::string device_name,
    fidl::InterfaceRequest<app::ServiceProvider> request) {
  auto it = net_connectors_.find(device_name);
  if (it == net_connectors_.end()) {
    return;
  }
  (*it).second.impl()->ConnectToServiceProvider(std::move(request));
}

}  // namespace ledger
