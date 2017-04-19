// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "adapter_manager.h"

#include <fcntl.h>

#include "apps/bluetooth/lib/hci/device_wrapper.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_printf.h"

namespace bluetooth_service {
namespace {

const char kBluetoothDeviceDir[] = "/dev/class/bt-hci";

}  // namespace

AdapterManager::AdapterManager() : weak_ptr_factory_(this) {
  device_watcher_ = mtl::DeviceWatcher::Create(
      kBluetoothDeviceDir, std::bind(&AdapterManager::OnDeviceFound, this, std::placeholders::_1,
                                     std::placeholders::_2));
}

AdapterManager::~AdapterManager() {
  for (auto& iter : adapters_) {
    iter.second->ShutDown([] {});
  }
  adapters_.clear();
}

ftl::RefPtr<bluetooth::gap::Adapter> AdapterManager::GetAdapter(
    const std::string& identifier) const {
  auto iter = adapters_.find(identifier);
  if (iter == adapters_.end()) return nullptr;
  return iter->second;
}

void AdapterManager::ForEachAdapter(const ForEachAdapterFunc& func) const {
  for (auto& iter : adapters_) {
    func(iter.second);
  }
}

bool AdapterManager::HasAdapters() const {
  return !adapters_.empty();
}

void AdapterManager::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void AdapterManager::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void AdapterManager::SetActiveAdapter(ftl::RefPtr<bluetooth::gap::Adapter> adapter) {
  if (active_adapter_ == adapter) return;

  active_adapter_ = adapter;
  for (auto& observer : observers_) observer.OnActiveAdapterChanged(active_adapter_);
}

void AdapterManager::OnDeviceFound(int dir_fd, std::string filename) {
  FTL_VLOG(1) << "bluetooth_service: AdapterManager: device found at "
              << ftl::StringPrintf("%s/%s", kBluetoothDeviceDir, filename.c_str());

  ftl::UniqueFD hci_dev_fd(openat(dir_fd, filename.c_str(), O_RDWR));
  if (!hci_dev_fd.is_valid()) {
    FTL_LOG(ERROR) << "bluetooth_service: AdapterManager: failed to open HCI device file: "
                   << strerror(errno);
    return;
  }

  auto hci_dev = std::make_unique<bluetooth::hci::MagentaDeviceWrapper>(std::move(hci_dev_fd));
  auto adapter = bluetooth::gap::Adapter::Create(std::move(hci_dev));
  auto self = weak_ptr_factory_.GetWeakPtr();

  // Called when Adapter initialization has completed.
  auto init_cb = [adapter, self](bool success) {
    if (!success) {
      FTL_VLOG(1) << "bluetooth_service: AdapterManager: failed to initialize adapter";
      return;
    }

    if (!self) {
      // AdapterManager was deleted before this callback was run.
      adapter->ShutDown([] {});
      return;
    }

    self->RegisterAdapter(adapter);
  };

  // Once initialized, this callback will be called when the underlying HCI device disconnects.
  auto disconnect_cb = [ self, id = adapter->identifier() ]() {
    if (self) self->OnAdapterTransportClosed(id);
  };

  adapter->Initialize(init_cb, disconnect_cb);
}

void AdapterManager::RegisterAdapter(ftl::RefPtr<bluetooth::gap::Adapter> adapter) {
  FTL_DCHECK(adapters_.find(adapter->identifier()) == adapters_.end());

  adapters_[adapter->identifier()] = adapter;

  for (auto& observer : observers_) observer.OnAdapterCreated(adapter);

  // If there is no current active adapter then assign it. This means that generally the first
  // adapter we see will be made active.
  // TODO(armansito): Either provide a mechanism for upper layers to enable/disable this policy or
  // remove it altogether. This may or may not be the behavior we want.
  if (!active_adapter_) SetActiveAdapter(adapter);
}

void AdapterManager::OnAdapterTransportClosed(std::string adapter_identifier) {
  auto iter = adapters_.find(adapter_identifier);
  FTL_DCHECK(iter != adapters_.end());

  FTL_VLOG(1) << "bluetooth_service: AdapterManager: Adapter transport closed: "
              << adapter_identifier;

  // Remove the adapter from the list so that it's no longer accessible to service clients. We
  // notify the delegate only after the adapter has been fully shut down.
  auto adapter = iter->second;
  adapters_.erase(iter);
  auto self = weak_ptr_factory_.GetWeakPtr();
  adapter->ShutDown([adapter, self]() {
    if (!self) return;
    if (adapter == self->active_adapter_) self->AssignNextActiveAdapter();
    for (auto& observer : self->observers_) observer.OnAdapterRemoved(adapter);
  });
}

void AdapterManager::AssignNextActiveAdapter() {
  auto adapter = adapters_.empty() ? nullptr : adapters_.begin()->second;
  SetActiveAdapter(adapter);
}

}  // namespace bluetooth_service
