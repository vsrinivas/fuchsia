// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "adapter_manager.h"

#include <fcntl.h>

#include <async/default.h>
#include <fbl/function.h>
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

Adapter::Adapter(bluetooth::control::AdapterInfoPtr info,
                 bluetooth::host::HostPtr host)
    : info_(std::move(info)), host_(std::move(host)) {
  FXL_DCHECK(info_);
  FXL_DCHECK(host_);
}

AdapterManager::AdapterManager()
    : initializing_(true), active_adapter_(nullptr), weak_ptr_factory_(this) {
  device_watcher_ = fsl::DeviceWatcher::Create(
      kBluetoothDeviceDir,
      fbl::BindMember(this, &AdapterManager::OnDeviceFound));
  FXL_DCHECK(device_watcher_);

  init_timeout_task_.set_deadline(zx::deadline_after(kInitTimeout).get());
  init_timeout_task_.set_handler(
      fbl::BindMember(this, &AdapterManager::OnInitTimeout));

  zx_status_t status = init_timeout_task_.Post(async_get_default());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "bluetooth: Failed to post init timeout task: "
                   << zx_status_get_string(status);
  }
}

AdapterManager::~AdapterManager() {
  // Make sure to cancel any timeout task before this gets destroyed.
  CancelInitTimeout();
}

void AdapterManager::GetActiveAdapter(ActiveAdapterCallback callback) {
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

void AdapterManager::ListAdapters(AdapterMapCallback callback) {
  if (!initializing_) {
    callback(adapters_);
    return;
  }

  auto self = weak_ptr_factory_.GetWeakPtr();
  pending_requests_.push([self, callback = std::move(callback)] {
    if (self) {
      callback(self->adapters_);
    }
  });
}

bool AdapterManager::SetActiveAdapter(const std::string& identifier) {
  if (initializing_)
    return false;

  auto iter = adapters_.find(identifier);
  if (iter == adapters_.end())
    return false;

  SetActiveAdapterInternal(iter->second.get());
  return true;
}

void AdapterManager::OnDeviceFound(int dir_fd, std::string filename) {
  FXL_VLOG(1) << "bluetooth: AdapterManager: device found at "
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

  fidl::InterfaceHandle<bluetooth::host::Host> handle(std::move(host_channel));
  FXL_DCHECK(handle.is_valid());

  // Bind the channel to a host interface pointer.
  auto host = handle.Bind();

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
        if (self)
          self->CreateAdapter(std::move(host), std::move(adapter_info));
      });

  host_raw->GetInfo(callback);
}

void AdapterManager::CreateAdapter(bluetooth::host::HostPtr host,
                                   bluetooth::control::AdapterInfoPtr info) {
  FXL_DCHECK(host);
  FXL_DCHECK(info);

  auto self = weak_ptr_factory_.GetWeakPtr();
  auto id = info->identifier;
  host.set_error_handler([self, id] {
    if (self)
      self->OnHostDisconnected(id);
  });

  auto adapter =
      std::unique_ptr<Adapter>(new Adapter(std::move(info), std::move(host)));
  auto* adapter_raw = adapter.get();

  adapters_[id] = std::move(adapter);

  if (!active_adapter_) {
    SetActiveAdapterInternal(adapter_raw);
  }

  if (adapter_added_cb_) {
    adapter_added_cb_(*adapter_raw);
  }

  // Leave the "initializing" state on the first adapter we see.
  if (initializing_) {
    CancelInitTimeout();
    ResolvePendingRequests();
  }
}

void AdapterManager::OnHostDisconnected(const std::string& identifier) {
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
    adapter_removed_cb_(*adapter);
  }
}

async_task_result_t AdapterManager::OnInitTimeout(async_t*,
                                                  zx_status_t status) {
  FXL_DCHECK(initializing_);

  if (status == ZX_OK) {
    ResolvePendingRequests();
  } else {
    FXL_VLOG(1) << "bluetooth: Init timeout fired with error: "
                << zx_status_get_string(status);
    initializing_ = false;
  }

  return ASYNC_TASK_FINISHED;
}

void AdapterManager::CancelInitTimeout() {
  zx_status_t status = init_timeout_task_.Cancel(async_get_default());
  if (status != ZX_OK) {
    FXL_VLOG(1) << "bluetooth: Failed to cancel init timeout task: "
                << zx_status_get_string(status);
  }
}

void AdapterManager::SetActiveAdapterInternal(Adapter* adapter) {
  FXL_DCHECK(adapter);

  if (active_adapter_) {
    // Tell the current active adapter to close all of its handles.
    FXL_DCHECK(active_adapter_->host());
    active_adapter_->host()->Close();
  }

  active_adapter_ = adapter;
  if (active_adapter_changed_cb_) {
    active_adapter_changed_cb_(active_adapter_);
  }
}

void AdapterManager::ResolvePendingRequests() {
  FXL_DCHECK(initializing_);

  initializing_ = false;

  while (!pending_requests_.empty()) {
    pending_requests_.front()();
    pending_requests_.pop();
  }
}

}  // namespace bluetooth_service
