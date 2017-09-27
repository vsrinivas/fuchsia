// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/device_map_impl.h"

#include <limits.h>
#include <unistd.h>

#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fxl/time/time_point.h"
#include "peridot/lib/fidl/json_xdr.h"
#include "peridot/lib/ledger_client/operations.h"
#include "peridot/lib/ledger_client/storage.h"

namespace modular {

namespace {

void XdrDeviceData(XdrContext* const xdr, DeviceMapEntry* const data) {
  xdr->Field("name", &data->name);
  xdr->Field("device_id", &data->device_id);
  xdr->Field("profile", &data->profile);
  xdr->Field("hostname", &data->hostname);

  // TODO(jimbe) Remove error handler after 2017-12-01
  // The time below is 26 Sep 2017 17:44:40 GMT, just to mark the entry as old.
  xdr->ReadErrorHandler([data] { data->last_change_timestamp = 1506447879; })
      ->Field("last_change_timestamp", &data->last_change_timestamp);
}

std::string LoadHostname() {
  char host_name_buffer[HOST_NAME_MAX + 1];
  int result = gethostname(host_name_buffer, sizeof(host_name_buffer));

  if (result < 0) {
    FXL_LOG(ERROR) << "unable to get hostname. errno " << errno;
    return "fuchsia";
  }

  return host_name_buffer;
}

}  // namespace

DeviceMapImpl::DeviceMapImpl(const std::string& device_name,
                             const std::string& device_id,
                             const std::string& device_profile,
                             LedgerClient* const ledger_client,
                             LedgerPageId page_id)
    : PageClient("DeviceMapImpl",
                 ledger_client,
                 std::move(page_id),
                 kDeviceKeyPrefix) {
  current_device_id_ = device_id;

  // TODO(jimbe) Load the DeviceMapEntry for the current device from the Ledger.
  DeviceMapEntryPtr device = DeviceMapEntry::New();
  device->name = device_name;
  device->device_id = device_id;
  device->profile = device_profile;
  device->hostname = LoadHostname();

  devices_[device_id] = std::move(device);
  SaveCurrentDevice();
}

DeviceMapImpl::~DeviceMapImpl() = default;

void DeviceMapImpl::Connect(fidl::InterfaceRequest<DeviceMap> request) {
  bindings_.AddBinding(this, std::move(request));
}

void DeviceMapImpl::Query(const QueryCallback& callback) {
  new ReadAllDataCall<DeviceMapEntry>(
      &operation_queue_, page(), kDeviceKeyPrefix, XdrDeviceData, callback);
}

void DeviceMapImpl::GetCurrentDevice(const GetCurrentDeviceCallback& callback) {
  callback(devices_[current_device_id_].Clone());
}

void DeviceMapImpl::SetCurrentDeviceProfile(const ::fidl::String& profile) {
  devices_[current_device_id_]->profile = profile;
  Notify(current_device_id_);
  SaveCurrentDevice();
}

void DeviceMapImpl::SaveCurrentDevice() {
  auto& device = devices_[current_device_id_];
  device->last_change_timestamp = time(nullptr);

  new WriteDataCall<DeviceMapEntry>(&operation_queue_, page(),
                                    MakeDeviceKey(current_device_id_),
                                    XdrDeviceData, device->Clone(), [] {});
}

void DeviceMapImpl::WatchDeviceMap(
    fidl::InterfaceHandle<DeviceMapWatcher> watcher) {
  auto watcher_ptr = DeviceMapWatcherPtr::Create(std::move(watcher));
  for (const auto& item : devices_) {
    const auto& device = item.second;
    watcher_ptr->OnDeviceMapChange(device->Clone());
  }
  change_watchers_.AddInterfacePtr(std::move(watcher_ptr));
}

void DeviceMapImpl::Notify(const std::string& device_id) {
  const DeviceMapEntryPtr& device = devices_[current_device_id_];
  change_watchers_.ForAllPtrs([&device](DeviceMapWatcher* watcher)
                              { watcher->OnDeviceMapChange(device.Clone()); });
}

void DeviceMapImpl::OnPageChange(const std::string& key,
                                 const std::string& value) {
  FXL_LOG(INFO) << "Updated Device: " << key << " value=" << value;

  auto device = DeviceMapEntry::New();
  if (!XdrRead(value, &device, XdrDeviceData)) {
    FXL_DCHECK(false);
    return;
  }

  const fidl::String& device_id = device->device_id;
  devices_[device_id] = std::move(device);
  Notify(device_id);
}

void DeviceMapImpl::OnPageDelete(const std::string& key) {
  // This shouldn't happen.
  FXL_LOG(ERROR) << "Deleted Device: " << key;
}

}  // namespace modular
