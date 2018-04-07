// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bluetooth_manager.h"

#include <fcntl.h>

#include <fbl/function.h>
#include <lib/async/default.h>
#include <zircon/status.h>

#include "garnet/lib/bluetooth/c/bt_host.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

namespace bluetooth_service {
namespace {

const char kBluetoothDeviceDir[] = "/dev/class/bt-host";

// We remain in the initializing state for at most 5 seconds.
constexpr zx::duration kInitTimeout = zx::sec(5);

}  // namespace

Adapter::Adapter(bluetooth_control::AdapterInfo info,
                 bluetooth_host::HostPtr host,
                 BluetoothManager::RemoteDeviceUpdatedCallback update_cb)
    : info_(std::move(info)),
      host_(std::move(host)),
      adapter_delegate_(this),
      update_cb_(update_cb) {
  FXL_DCHECK(host_);
  FXL_DCHECK(update_cb_);

  host_->RequestAdapter(host_adapter_.NewRequest());

  bluetooth_host::AdapterDelegatePtr delegate;
  adapter_delegate_.Bind(delegate.NewRequest());

  host_adapter_->SetDelegate(std::move(delegate));

  // Get the initial adapter info.
  host_adapter_->GetInfo([this](auto info) { info.Clone(&info_); });
}

void Adapter::StartDiscovery(
    std::unique_ptr<DiscoveryRequestToken> token,
    BluetoothManager::DiscoveryRequestCallback callback) const {
  FXL_DCHECK(token);
  FXL_DCHECK(callback);

  if (info_.state->discovering->value) {
    callback(std::move(token), "Already discovering");
    return;
  }

  host_adapter_->StartDiscovery(
      fxl::MakeCopyable([token = std::move(token),
                         callback = std::move(callback)](auto status) mutable {
        if (status.error) {
          callback(nullptr, status.error->description);
          return;
        }
        callback(std::move(token), "Started discovery");
      }));
};

void Adapter::StopDiscovery() const {
  // TODO(jamuraa): Do something when we fail to stop discovery?
  host_adapter_->StopDiscovery([](auto) {});
};

void Adapter::OnAdapterStateChanged(bluetooth_control::AdapterState state) {
  if (state.local_name) {
    info_.state->local_name = state.local_name;
  }
  if (state.discoverable) {
    info_.state->discoverable = std::move(state.discoverable);
  }
  if (state.discovering) {
    info_.state->discovering = std::move(state.discovering);
  }
  if (state.local_service_uuids) {
    info_.state->local_service_uuids = std::move(state.local_service_uuids);
  }
};

void Adapter::OnDeviceDiscovered(bluetooth_control::RemoteDevice device) {
  update_cb_(device);
};

BluetoothManager::BluetoothManager()
    : initializing_(true),
      active_adapter_(nullptr),
      init_timeout_task_(
          fbl::BindMember(this, &BluetoothManager::OnInitTimeout)),
      weak_ptr_factory_(this) {
  device_watcher_ = fsl::DeviceWatcher::Create(
      kBluetoothDeviceDir,
      fbl::BindMember(this, &BluetoothManager::OnHostFound));
  FXL_DCHECK(device_watcher_);

  zx_status_t status =
      init_timeout_task_.PostDelayed(async_get_default(), kInitTimeout);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "bluetooth: Failed to post init timeout task: "
                   << zx_status_get_string(status);
  }
}

DiscoveryRequestToken::DiscoveryRequestToken(
    fxl::WeakPtr<BluetoothManager> vendor)
    : vendor_(vendor) {
  FXL_DCHECK(vendor_);
}

DiscoveryRequestToken::~DiscoveryRequestToken() {
  if (!vendor_) {
    return;
  }
  vendor_->RemoveDiscoveryRequest(this);
}

BluetoothManager::~BluetoothManager() {
  // Make sure to cancel any timeout task before this gets destroyed.
  CancelInitTimeout();
}

void BluetoothManager::GetActiveAdapter(ActiveAdapterCallback callback) {
  if (!initializing_) {
    callback(active_adapter_);
    return;
  }

  auto self = weak_ptr_factory_.GetWeakPtr();
  pending_requests_.push([self, callback = std::move(callback)] {
    if (self) {
      callback(self->active_adapter_);
    }
  });
}

void BluetoothManager::GetKnownAdapters(AdapterInfoMapCallback callback) {
  if (!initializing_) {
    callback(GetAdapterInfoMap());
    return;
  }

  auto self = weak_ptr_factory_.GetWeakPtr();
  pending_requests_.push([self, callback = std::move(callback)] {
    if (self) {
      callback(self->GetAdapterInfoMap());
    }
  });
}

bool BluetoothManager::SetActiveAdapter(const std::string& identifier) {
  if (initializing_) {
    return false;
  }

  auto iter = adapters_.find(identifier);
  if (iter == adapters_.end()) {
    return false;
  }

  SetActiveAdapterInternal(iter->second.get());
  return true;
}

void BluetoothManager::RequestDiscovery(DiscoveryRequestCallback callback) {
  std::unique_ptr<DiscoveryRequestToken> token(
      new DiscoveryRequestToken(weak_ptr_factory_.GetWeakPtr()));
  if (!discovery_requests_.empty()) {
    discovery_requests_.insert(token.get());
    callback(std::move(token), "Already discovering");
    return;
  }

  auto self = weak_ptr_factory_.GetWeakPtr();
  GetActiveAdapter(fxl::MakeCopyable(
      [self, callback = std::move(callback),
       token = std::move(token)](const auto* adapter) mutable {
        if (!self) {
          callback(nullptr, "BluetoothManager invalid");
          return;
        }
        self->discovery_requests_.insert(token.get());
        adapter->StartDiscovery(std::move(token), std::move(callback));
      }));
}

void BluetoothManager::RemoveDiscoveryRequest(DiscoveryRequestToken* token) {
  FXL_DCHECK(token);
  discovery_requests_.erase(token);
  if (discovery_requests_.empty()) {
    GetActiveAdapter([](const auto* adapter) { adapter->StopDiscovery(); });
  }
}

const BluetoothManager::AdapterInfoMap BluetoothManager::GetAdapterInfoMap()
    const {
  AdapterInfoMap result;
  bluetooth_control::AdapterInfo info;
  for (const auto& a : adapters_) {
    a.second->info().Clone(&info);
    result.emplace(a.first, std::move(info));
  }
  return result;
}

void BluetoothManager::OnHostFound(int dir_fd, std::string filename) {
  FXL_VLOG(1) << "bluetooth: BluetoothManager: device found at "
              << fxl::StringPrintf("%s/%s", kBluetoothDeviceDir,
                                   filename.c_str());

  fxl::UniqueFD dev(openat(dir_fd, filename.c_str(), O_RDWR));
  if (!dev.is_valid()) {
    FXL_LOG(ERROR) << "bluetooth: failed to open bt-host device: "
                   << strerror(errno);
    return;
  }

  zx::channel host_channel;
  ssize_t status = ioctl_bt_host_open_channel(
      dev.get(), host_channel.reset_and_get_address());
  if (status < 0) {
    FXL_LOG(ERROR) << "bluetooth: Failed to open Host channel: "
                   << zx_status_get_string(status);
    return;
  }

  FXL_DCHECK(host_channel);

  fidl::InterfaceHandle<bluetooth_host::Host> handle(std::move(host_channel));
  FXL_DCHECK(handle.is_valid());

  // Bind the channel to a host interface pointer.
  // Wrap in a unique_ptr so we can rely on the location of the HostPtr when it
  // is moved into the callback.
  auto host = std::make_unique<bluetooth_host::HostPtr>(handle.Bind());

  // We create and store an Adapter for |host| only when GetInfo() succeeds.
  // Ownership of |host| is passed to |callback| only when we receive a response
  // from GetInfo().
  //
  // If a response is not received (e.g. because the handle was
  // closed) then |callback| will never execute. In that case |host| will be
  // destroyed along with |callback|.
  auto* host_raw = host.get();
  auto self = weak_ptr_factory_.GetWeakPtr();
  auto callback = fxl::MakeCopyable(
      [self, host = std::move(host)](auto adapter_info) mutable {
        if (self) {
          self->CreateAdapter(std::move(*host), std::move(adapter_info));
        }
      });

  (*host_raw)->GetInfo(callback);
}

void BluetoothManager::CreateAdapter(bluetooth_host::HostPtr host,
                                     bluetooth_control::AdapterInfo info) {
  FXL_DCHECK(host);

  auto self = weak_ptr_factory_.GetWeakPtr();
  auto id = info.identifier;
  host.set_error_handler([self, id] {
    if (self)
      self->OnHostDisconnected(id);
  });

  auto adapter = std::unique_ptr<Adapter>(new Adapter(
      std::move(info), std::move(host),
      fbl::BindMember(this, &BluetoothManager::OnRemoteDeviceUpdated)));
  auto* adapter_raw = adapter.get();

  adapters_[id] = std::move(adapter);

  if (!active_adapter_) {
    SetActiveAdapterInternal(adapter_raw);
  }

  if (adapter_updated_cb_) {
    auto info = bluetooth_control::AdapterInfo::New();
    adapter_raw->info().Clone(info.get());
    adapter_updated_cb_(std::move(info));
  }

  // Leave the "initializing" state on the first adapter we see.
  if (initializing_) {
    CancelInitTimeout();
    ResolvePendingRequests();
  }
}

void BluetoothManager::OnHostDisconnected(const std::string& identifier) {
  auto iter = adapters_.find(identifier);
  FXL_DCHECK(iter != adapters_.end());

  FXL_VLOG(1) << "bluetooth: Adapter removed: " << identifier;

  // Remove the adapter from the list so that it is no longer accessible to
  // service clients.
  auto adapter = std::move(iter->second);
  adapters_.erase(iter);

  FXL_DCHECK(adapter);

  // If the active adapter was removed then we assign the next available one as
  // active.
  if (adapter.get() == active_adapter_) {
    SetActiveAdapterInternal(
        adapters_.empty() ? nullptr : adapters_.begin()->second.get());
  }

  if (adapter_removed_cb_) {
    adapter_removed_cb_(adapter->info().identifier);
  }
}

void BluetoothManager::OnRemoteDeviceUpdated(
    const bluetooth_control::RemoteDevice& device) {
  if (device_updated_cb_) {
    device_updated_cb_(device);
  }
}

void BluetoothManager::OnInitTimeout(async_t*,
                                     async::Task*,
                                     zx_status_t status) {
  FXL_DCHECK(initializing_);

  if (status == ZX_OK) {
    ResolvePendingRequests();
  } else {
    FXL_VLOG(1) << "bluetooth: Init timeout fired with error: "
                << zx_status_get_string(status);
    initializing_ = false;
  }
}

void BluetoothManager::CancelInitTimeout() {
  zx_status_t status = init_timeout_task_.Cancel(async_get_default());
  if (status != ZX_OK) {
    FXL_VLOG(1) << "bluetooth: Failed to cancel init timeout task: "
                << zx_status_get_string(status);
  }
}

void BluetoothManager::SetActiveAdapterInternal(Adapter* adapter) {
  // Tell the current active adapter to close all of its handle, if there is an
  // active adapter and its host interface handle is still bound. The host
  // handle can be unbound if this was called by OnHostDisconnected().
  if (active_adapter_ && active_adapter_->host()) {
    active_adapter_->host()->Close();
  }

  active_adapter_ = adapter;

  if (!active_adapter_changed_cb_) {
    return;
  }

  if (!active_adapter_) {
    active_adapter_changed_cb_(nullptr);
    return;
  }

  auto info = bluetooth_control::AdapterInfo::New();
  active_adapter_->info().Clone(info.get());
  active_adapter_changed_cb_(std::move(info));
}

void BluetoothManager::ResolvePendingRequests() {
  FXL_DCHECK(initializing_);

  initializing_ = false;

  while (!pending_requests_.empty()) {
    pending_requests_.front()();
    pending_requests_.pop();
  }
}

}  // namespace bluetooth_service
