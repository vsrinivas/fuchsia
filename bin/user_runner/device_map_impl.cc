// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/device_map_impl.h"

#include <limits.h>
#include <unistd.h>

#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/time/time_point.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/fidl/json_xdr.h"
#include "peridot/lib/ledger/operations.h"
#include "peridot/lib/ledger/storage.h"
#include "peridot/lib/rapidjson/rapidjson.h"

namespace modular {

namespace {

void XdrDeviceData(XdrContext* const xdr, DeviceMapEntry* const data) {
  xdr->Field("name", &data->name);
  xdr->Field("device_id", &data->device_id);
  xdr->Field("profile", &data->profile);
  xdr->Field("hostname", &data->hostname);
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
  current_device_.name = device_name;
  current_device_.device_id = device_id;
  current_device_.profile = device_profile;
  current_device_.hostname = LoadHostname();

  new WriteDataCall<DeviceMapEntry, fidl::InlinedStructPtr<DeviceMapEntry>>(
      &operation_queue_, page(), MakeDeviceKey(device_id), XdrDeviceData,
      current_device_.Clone(), [] {});
}

DeviceMapImpl::~DeviceMapImpl() = default;

void DeviceMapImpl::Connect(fidl::InterfaceRequest<DeviceMap> request) {
  bindings_.AddBinding(this, std::move(request));
}

void DeviceMapImpl::Query(const QueryCallback& callback) {
  new ReadAllDataCall<DeviceMapEntry, fidl::InlinedStructPtr<DeviceMapEntry>>(
      &operation_queue_, page(), kDeviceKeyPrefix, XdrDeviceData, callback);
}

void DeviceMapImpl::GetCurrentDevice(const GetCurrentDeviceCallback& callback) {
  callback(current_device_.Clone());
}

void DeviceMapImpl::OnPageChange(const std::string& key,
                                 const std::string& /*value*/) {
  FXL_LOG(INFO) << "New Device: " << key;
}

void DeviceMapImpl::OnPageDelete(const std::string& key) {
  FXL_LOG(INFO) << "Deleted Device: " << key;
}

}  // namespace modular
