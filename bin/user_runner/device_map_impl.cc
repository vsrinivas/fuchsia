// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/device_map_impl.h"

#include <limits.h>
#include <unistd.h>

#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_ptr.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fxl/time/time_point.h>

#include "peridot/bin/user_runner/storage/constants_and_utils.h"
#include "peridot/lib/fidl/json_xdr.h"
#include "peridot/lib/ledger_client/operations.h"

namespace modular {

namespace {

using ReadDeviceDataCall = ReadDataCall<fuchsia::modular::DeviceMapEntry>;
using ReadAllDeviceDataCall = ReadAllDataCall<fuchsia::modular::DeviceMapEntry>;
using WriteDeviceDataCall = WriteDataCall<fuchsia::modular::DeviceMapEntry>;

// Reads old versions of device data, which are missing a timestamp.
void XdrDeviceMapEntry_v1(XdrContext* const xdr,
                          fuchsia::modular::DeviceMapEntry* const data) {
  xdr->Field("name", &data->name);
  xdr->Field("device_id", &data->device_id);
  xdr->Field("profile", &data->profile);
  xdr->Field("hostname", &data->hostname);

  // The time below is 26 Sep 2017 17:44:40 GMT, just to mark the entry as old.
  // Since this filter is not the latest, it is only ever used FROM_JSON, never
  // TO_JSON.
  data->last_change_timestamp = 1506447879;
}

void XdrDeviceMapEntry_v2(XdrContext* const xdr,
                          fuchsia::modular::DeviceMapEntry* const data) {
  xdr->Field("name", &data->name);
  xdr->Field("device_id", &data->device_id);
  xdr->Field("profile", &data->profile);
  xdr->Field("hostname", &data->hostname);
  xdr->Field("last_change_timestamp", &data->last_change_timestamp);
}

void XdrDeviceMapEntry_v3(XdrContext* const xdr,
                          fuchsia::modular::DeviceMapEntry* const data) {
  if (!xdr->Version(3)) {
    return;
  }
  xdr->Field("name", &data->name);
  xdr->Field("device_id", &data->device_id);
  xdr->Field("profile", &data->profile);
  xdr->Field("hostname", &data->hostname);
  xdr->Field("last_change_timestamp", &data->last_change_timestamp);
}

constexpr XdrFilterType<fuchsia::modular::DeviceMapEntry> XdrDeviceMapEntry[] =
    {
        XdrDeviceMapEntry_v3,
        XdrDeviceMapEntry_v2,
        XdrDeviceMapEntry_v1,
        nullptr,
};

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
    : PageClient("DeviceMapImpl", ledger_client, std::move(page_id),
                 kDeviceKeyPrefix) {
  current_device_id_ = device_id;

  // TODO(jimbe) Load the fuchsia::modular::DeviceMapEntry for the current
  // device from the Ledger.
  fuchsia::modular::DeviceMapEntry device;
  device.name = device_name;
  device.device_id = device_id;
  device.profile = device_profile;
  device.hostname = LoadHostname();

  devices_[device_id] = std::move(device);
  SaveCurrentDevice();
}

DeviceMapImpl::~DeviceMapImpl() = default;

void DeviceMapImpl::Connect(
    fidl::InterfaceRequest<fuchsia::modular::DeviceMap> request) {
  bindings_.AddBinding(this, std::move(request));
}

void DeviceMapImpl::Query(QueryCallback callback) {
  operation_queue_.Add(new ReadAllDeviceDataCall(page(), kDeviceKeyPrefix,
                                                 XdrDeviceMapEntry, callback));
}

void DeviceMapImpl::GetCurrentDevice(GetCurrentDeviceCallback callback) {
  callback(devices_[current_device_id_]);
}

void DeviceMapImpl::SetCurrentDeviceProfile(::fidl::StringPtr profile) {
  devices_[current_device_id_].profile = profile;
  Notify(current_device_id_);
  SaveCurrentDevice();
}

void DeviceMapImpl::SaveCurrentDevice() {
  auto& device = devices_[current_device_id_];
  device.last_change_timestamp = time(nullptr);

  operation_queue_.Add(new WriteDeviceDataCall(
      page(), MakeDeviceKey(current_device_id_), XdrDeviceMapEntry,
      fidl::MakeOptional(device), [] {}));
}

void DeviceMapImpl::WatchDeviceMap(
    fidl::InterfaceHandle<fuchsia::modular::DeviceMapWatcher> watcher) {
  auto watcher_ptr = watcher.Bind();
  for (const auto& item : devices_) {
    const auto& device = item.second;
    watcher_ptr->OnDeviceMapChange(device);
  }
  change_watchers_.AddInterfacePtr(std::move(watcher_ptr));
}

void DeviceMapImpl::Notify(const std::string& device_id) {
  const fuchsia::modular::DeviceMapEntry& device = devices_[current_device_id_];
  for (auto& watcher : change_watchers_.ptrs()) {
    (*watcher)->OnDeviceMapChange(device);
  }
}

void DeviceMapImpl::OnPageChange(const std::string& key,
                                 const std::string& value) {
  FXL_LOG(INFO) << "Updated Device: " << key << " value=" << value;

  fuchsia::modular::DeviceMapEntry device;
  if (!XdrRead(value, &device, XdrDeviceMapEntry)) {
    FXL_DCHECK(false);
    return;
  }

  fidl::StringPtr device_id = device.device_id;
  devices_[device_id] = std::move(device);
  Notify(device_id);
}

void DeviceMapImpl::OnPageDelete(const std::string& key) {
  // This shouldn't happen.
  FXL_LOG(ERROR) << "Deleted Device: " << key;
}

}  // namespace modular
