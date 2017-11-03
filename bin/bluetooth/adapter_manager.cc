// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "adapter_manager.h"

#include <fcntl.h>

#include "garnet/drivers/bluetooth/lib/gap/adapter.h"
#include "garnet/drivers/bluetooth/lib/hci/device_wrapper.h"
#include "garnet/drivers/bluetooth/lib/hci/transport.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

namespace bluetooth_service {
namespace {

const char kBluetoothDeviceDir[] = "/dev/class/bt-hci";

}  // namespace

// Default no-op implementations for optional Observer methods.
void AdapterManager::Observer::OnAdapterCreated(
    bluetooth::gap::Adapter* adapter) {}
void AdapterManager::Observer::OnAdapterRemoved(
    bluetooth::gap::Adapter* adapter) {}

AdapterManager::AdapterManager() : weak_ptr_factory_(this) {
  device_watcher_ = fsl::DeviceWatcher::Create(
      kBluetoothDeviceDir,
      std::bind(&AdapterManager::OnDeviceFound, this, std::placeholders::_1,
                std::placeholders::_2));
}

AdapterManager::~AdapterManager() {
  for (auto& iter : adapters_) {
    iter.second->ShutDown();
  }
  adapters_.clear();
}

fxl::WeakPtr<bluetooth::gap::Adapter> AdapterManager::GetAdapter(
    const std::string& identifier) const {
  auto iter = adapters_.find(identifier);
  if (iter == adapters_.end())
    return fxl::WeakPtr<bluetooth::gap::Adapter>();
  return iter->second->AsWeakPtr();
}

void AdapterManager::ForEachAdapter(const ForEachAdapterFunc& func) const {
  for (auto& iter : adapters_) {
    func(iter.second.get());
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

fxl::WeakPtr<bluetooth::gap::Adapter> AdapterManager::GetActiveAdapter() {
  if (active_adapter_)
    return active_adapter_->AsWeakPtr();
  return fxl::WeakPtr<bluetooth::gap::Adapter>();
}

bool AdapterManager::SetActiveAdapter(const std::string& identifier) {
  auto iter = adapters_.find(identifier);
  if (iter == adapters_.end())
    return false;

  auto adapter = iter->second.get();

  return SetActiveAdapterInternal(adapter);
}

bool AdapterManager::SetActiveAdapterInternal(
    bluetooth::gap::Adapter* adapter) {
  // Return true if the adapter is already assigned.
  if (active_adapter_ == adapter)
    return true;

  active_adapter_ = adapter;
  for (auto& observer : observers_)
    observer.OnActiveAdapterChanged(active_adapter_);

  return true;
}

void AdapterManager::OnDeviceFound(int dir_fd, std::string filename) {
  FXL_VLOG(1) << "bluetooth_service: AdapterManager: device found at "
              << fxl::StringPrintf("%s/%s", kBluetoothDeviceDir,
                                   filename.c_str());

  fxl::UniqueFD hci_dev_fd(openat(dir_fd, filename.c_str(), O_RDWR));
  if (!hci_dev_fd.is_valid()) {
    FXL_LOG(ERROR)
        << "bluetooth_service: AdapterManager: failed to open HCI device file: "
        << strerror(errno);
    return;
  }

  auto hci_dev = std::make_unique<bluetooth::hci::ZirconDeviceWrapper>(
      std::move(hci_dev_fd));
  auto hci = bluetooth::hci::Transport::Create(std::move(hci_dev));
  auto adapter = std::make_unique<bluetooth::gap::Adapter>(std::move(hci));

  auto self = weak_ptr_factory_.GetWeakPtr();

  // TODO(armansito): Storing the raw pointer here so that it can be accessed
  // after moving |adapter| is ugly. Instead add a factory method that
  // asynchronously returns the adapter unique_ptr and change signature of
  // |disconnect_cb| to take in an Adapter reference.
  auto adapter_ptr = adapter.get();

  // Called when Adapter initialization has completed.
  auto init_cb = fxl::MakeCopyable([ adapter = std::move(adapter),
                                     self ](bool success) mutable {
    if (!success) {
      FXL_VLOG(1)
          << "bluetooth_service: AdapterManager: failed to initialize adapter";
      return;
    }

    if (!self) {
      // AdapterManager was deleted before this callback was run.
      adapter->ShutDown();
      return;
    }

    self->RegisterAdapter(std::move(adapter));
  });

  // Once initialized, this callback will be called when the underlying HCI
  // device disconnects.
  auto disconnect_cb = [ self, id = adapter_ptr->identifier() ]() {
    if (self)
      self->OnAdapterTransportClosed(id);
  };

  adapter_ptr->Initialize(init_cb, disconnect_cb);
}

void AdapterManager::RegisterAdapter(
    std::unique_ptr<bluetooth::gap::Adapter> adapter) {
  FXL_DCHECK(adapters_.find(adapter->identifier()) == adapters_.end());

  auto ptr = adapter.get();
  adapters_[adapter->identifier()] = std::move(adapter);

  for (auto& observer : observers_)
    observer.OnAdapterCreated(ptr);

  // If there is no current active adapter then assign it. This means that
  // generally the first adapter we see will be made active.
  // TODO(armansito): Either provide a mechanism for upper layers to
  // enable/disable this policy or remove it altogether. This may or may not be
  // the behavior we want.
  if (!active_adapter_)
    SetActiveAdapterInternal(ptr);
}

void AdapterManager::OnAdapterTransportClosed(std::string adapter_identifier) {
  auto iter = adapters_.find(adapter_identifier);
  FXL_DCHECK(iter != adapters_.end());

  FXL_VLOG(1) << "bluetooth_service: AdapterManager: Adapter transport closed: "
              << adapter_identifier;

  // Remove the adapter from the list so that it's no longer accessible to
  // service clients. We notify the delegate only after the adapter has been
  // fully shut down.
  auto adapter = std::move(iter->second);
  adapters_.erase(iter);
  adapter->ShutDown();

  if (adapter.get() == active_adapter_)
    AssignNextActiveAdapter();
  for (auto& observer : observers_)
    observer.OnAdapterRemoved(adapter.get());
}

void AdapterManager::AssignNextActiveAdapter() {
  SetActiveAdapterInternal(adapters_.empty() ? nullptr
                                             : adapters_.begin()->second.get());
}

}  // namespace bluetooth_service
