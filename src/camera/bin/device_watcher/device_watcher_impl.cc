// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/device_watcher/device_watcher_impl.h"

#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/camera3/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/syslog/cpp/logger.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <algorithm>
#include <limits>

DeviceWatcherImpl::DeviceWatcherImpl() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

DeviceWatcherImpl::~DeviceWatcherImpl() { loop_.Shutdown(); }

fit::result<std::unique_ptr<DeviceWatcherImpl>, zx_status_t> DeviceWatcherImpl::Create() {
  auto server = std::make_unique<DeviceWatcherImpl>();

  zx_status_t status = server->loop_.StartThread("DeviceWatcherImpl");
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }

  return fit::ok(std::move(server));
}

fit::result<PersistentDeviceId, zx_status_t> DeviceWatcherImpl::AddDevice(
    fidl::InterfaceHandle<fuchsia::camera2::hal::Controller> controller) {
  FX_LOGS(DEBUG) << "AddDevice(...)";
  fuchsia::camera2::hal::ControllerPtr ctrl;
  zx_status_t status = ctrl.Bind(std::move(controller), loop_.dispatcher());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }

  // Try to get the device info, waiting for either the info to be returned, or for an error.
  zx::event event;
  zx::event::create(0, &event);
  constexpr auto kErrorSignal = ZX_USER_SIGNAL_0;
  constexpr auto kInfoSignal = ZX_USER_SIGNAL_1;

  zx_status_t status_return;
  ctrl.set_error_handler([&](zx_status_t status) {
    FX_PLOGS(ERROR, status);
    status_return = status;
    event.signal(0, kErrorSignal);
  });

  fuchsia::camera2::DeviceInfo info_return;
  ctrl->GetDeviceInfo([&](fuchsia::camera2::DeviceInfo info) {
    info_return = std::move(info);
    event.signal(0, kInfoSignal);
  });

  zx_signals_t signaled{};
  status = event.wait_one(kErrorSignal | kInfoSignal, zx::time::infinite(), &signaled);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }

  if (signaled & kErrorSignal) {
    FX_PLOGS(ERROR, status_return);
    return fit::error(status_return);
  }

  ZX_ASSERT(signaled & kInfoSignal);

  if (!info_return.has_vendor_id() || !info_return.has_product_id()) {
    FX_LOGS(ERROR) << "Controller missing vendor or product ID.";
    return fit::error(ZX_ERR_NOT_SUPPORTED);
  }

  // Determine the persistent ID for this device and create/update its entry.

  // TODO(fxb/43565): This generates the same ID for multiple instances of the same device. It
  // should be made unique by incorporating a truly unique value such as the bus ID.
  constexpr uint32_t kVendorShift = 16;
  PersistentDeviceId persistent_id =
      (static_cast<uint64_t>(info_return.vendor_id()) << kVendorShift) | info_return.product_id();

  ctrl.set_error_handler([this, persistent_id](zx_status_t status) {
    FX_PLOGS(INFO, status) << "Camera " << persistent_id << " disconnected";
    std::lock_guard<std::mutex> lock(devices_lock_);
    devices_[persistent_id].controller = nullptr;
    for (auto& client : clients_) {
      client.second->UpdateDevices(devices_);
    }
  });

  std::lock_guard<std::mutex> lock(devices_lock_);
  devices_[persistent_id] = {.id = device_id_next_, .controller = ctrl.Unbind()};

  FX_LOGS(DEBUG) << "Added device " << persistent_id << " as device ID " << device_id_next_;
  ++device_id_next_;

  return fit::ok(persistent_id);
}

zx_status_t DeviceWatcherImpl::RemoveDevice(PersistentDeviceId id) {
  FX_LOGS(DEBUG) << "RemoveDevice(" << id << ")";

  std::lock_guard<std::mutex> lock(devices_lock_);
  auto it = devices_.find(id);
  if (it == devices_.end()) {
    FX_PLOGS(ERROR, ZX_ERR_NOT_FOUND);
    return ZX_ERR_NOT_FOUND;
  }
  devices_.erase(it);

  return ZX_OK;
}

void DeviceWatcherImpl::UpdateClients() {
  FX_LOGS(DEBUG) << "UpdateClients()";

  std::lock_guard<std::mutex> lock(devices_lock_);
  for (auto& client : clients_) {
    client.second->UpdateDevices(devices_);
  }
}

fidl::InterfaceRequestHandler<fuchsia::camera3::DeviceWatcher> DeviceWatcherImpl::GetHandler() {
  return fit::bind_member(this, &DeviceWatcherImpl::OnNewRequest);
}

void DeviceWatcherImpl::OnNewRequest(
    fidl::InterfaceRequest<fuchsia::camera3::DeviceWatcher> request) {
  auto result = Client::Create(client_id_next_, std::move(request), loop_.dispatcher());
  if (result.is_error()) {
    FX_PLOGS(ERROR, result.error());
    return;
  }
  clients_[client_id_next_] = result.take_value();
  FX_LOGS(DEBUG) << "DeviceWatcher client " << client_id_next_ << " connected.";
  ++client_id_next_;
}

DeviceWatcherImpl::Client::Client() : binding_(this) {}

fit::result<std::unique_ptr<DeviceWatcherImpl::Client>, zx_status_t>
DeviceWatcherImpl::Client::Create(ClientId id,
                                  fidl::InterfaceRequest<fuchsia::camera3::DeviceWatcher> request,
                                  async_dispatcher_t* dispatcher) {
  auto client = std::make_unique<DeviceWatcherImpl::Client>();

  client->id_ = id;

  zx_status_t status = client->binding_.Bind(request.TakeChannel(), dispatcher);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }

  client->binding_.set_error_handler([client = client.get()](zx_status_t status) {
    FX_PLOGS(DEBUG, status) << "DeviceWatcher client " << client->id_ << " disconnected.";
  });

  return fit::ok(std::move(client));
}

void DeviceWatcherImpl::Client::UpdateDevices(const DevicesMap& devices) {
  last_known_ids_.clear();
  for (const auto& device : devices) {
    if (device.second.controller) {
      last_known_ids_.insert(device.second.id);
    }
  }
  CheckDevicesChanged();
}

DeviceWatcherImpl::Client::operator bool() { return binding_.is_bound(); }

// Constructs the set of events that should be returned to the client, or null if the response
// should not be sent.
static std::optional<std::vector<fuchsia::camera3::WatchDevicesEvent>> BuildEvents(
    const std::set<TransientDeviceId>& last_known,
    const std::optional<std::set<TransientDeviceId>>& last_sent) {
  std::vector<fuchsia::camera3::WatchDevicesEvent> events;

  // If never sent, just populate with Added events for the known IDs.
  if (!last_sent.has_value()) {
    for (auto id : last_known) {
      events.push_back(fuchsia::camera3::WatchDevicesEvent::WithAdded(std::move(id)));
    }
    return events;
  }

  // Otherwise, build a full event list.
  std::set<TransientDeviceId> existing;
  std::set<TransientDeviceId> added;
  std::set<TransientDeviceId> removed;

  // Exisitng = Known && Sent
  std::set_intersection(last_known.begin(), last_known.end(), last_sent.value().begin(),
                        last_sent.value().end(), std::inserter(existing, existing.begin()));

  // Added = Known - Sent
  std::set_difference(last_known.begin(), last_known.end(), last_sent.value().begin(),
                      last_sent.value().end(), std::inserter(added, added.begin()));

  // Removed = Sent - Known
  std::set_difference(last_sent.value().begin(), last_sent.value().end(), last_known.begin(),
                      last_known.end(), std::inserter(removed, removed.begin()));

  if (added.empty() && removed.empty()) {
    return std::nullopt;
  }

  for (auto id : existing) {
    events.push_back(fuchsia::camera3::WatchDevicesEvent::WithExisting(std::move(id)));
  }
  for (auto id : added) {
    events.push_back(fuchsia::camera3::WatchDevicesEvent::WithAdded(std::move(id)));
  }
  for (auto id : removed) {
    events.push_back(fuchsia::camera3::WatchDevicesEvent::WithRemoved(std::move(id)));
  }

  return events;
};

void DeviceWatcherImpl::Client::CheckDevicesChanged() {
  if (!callback_) {
    return;
  }

  auto events = BuildEvents(last_known_ids_, last_sent_ids_);
  if (!events.has_value()) {
    return;
  }

  callback_(std::move(events.value()));

  callback_ = nullptr;

  last_sent_ids_ = last_known_ids_;
}

void DeviceWatcherImpl::Client::WatchDevices(WatchDevicesCallback callback) {
  if (callback_) {
    FX_PLOGS(INFO, ZX_ERR_BAD_STATE)
        << "Client called WatchDevices while a previous call was still pending.";
    binding_.Close(ZX_ERR_BAD_STATE);
    return;
  }

  callback_ = std::move(callback);

  CheckDevicesChanged();
}

void DeviceWatcherImpl::Client::ConnectToDevice(
    TransientDeviceId id, fidl::InterfaceRequest<fuchsia::camera3::Device> request) {
  request.Close(ZX_ERR_NOT_SUPPORTED);
}
