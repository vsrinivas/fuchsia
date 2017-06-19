// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/user_runner/device_map_impl.h"

#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/fidl/json_xdr.h"
#include "apps/modular/lib/ledger/operations.h"
#include "apps/modular/lib/ledger/storage.h"
#include "apps/modular/lib/rapidjson/rapidjson.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/ftl/time/time_point.h"
#include "lib/mtl/vmo/strings.h"

namespace modular {

namespace {

void XdrDeviceData(XdrContext* const xdr, DeviceMapEntry* const data) {
  xdr->Field("name", &data->name);
  xdr->Field("device_id", &data->device_id);
  xdr->Field("profile", &data->profile);
}

void WriteDeviceData(OperationContainer* const operation_container,
                     const std::string& device_name,
                     const std::string& device_id,
                     const std::string& profile,
                     ledger::Page* const page) {
  DeviceMapEntryPtr data = DeviceMapEntry::New();
  data->name = device_name;
  data->device_id = device_id;
  data->profile = profile;

  new WriteDataCall<DeviceMapEntry, fidl::InlinedStructPtr<DeviceMapEntry>>(
      operation_container, page, MakeDeviceKey(device_id), XdrDeviceData,
      std::move(data), [] {});
}

}  // namespace

DeviceMapImpl::DeviceMapImpl(const std::string& device_name,
                             const std::string& device_id,
                             const std::string& device_profile,
                             ledger::Page* const page)
    : PageClient("DeviceMapImpl", page, kDeviceKeyPrefix), page_(page) {
  WriteDeviceData(&operation_queue_, device_name, device_id, device_profile,
                  page_);
}

DeviceMapImpl::~DeviceMapImpl() = default;

void DeviceMapImpl::Connect(fidl::InterfaceRequest<DeviceMap> request) {
  bindings_.AddBinding(this, std::move(request));
}

void DeviceMapImpl::Query(const QueryCallback& callback) {
  new ReadAllDataCall<DeviceMapEntry, fidl::InlinedStructPtr<DeviceMapEntry>>(
      &operation_queue_, page_, kDeviceKeyPrefix, XdrDeviceData, callback);
}

void DeviceMapImpl::OnChange(const std::string& key, const std::string& value) {
  FTL_LOG(INFO) << "New Device: " << key;
}

void DeviceMapImpl::OnDelete(const std::string& key) {
  FTL_LOG(INFO) << "Deleted Device: " << key;
}

}  // namespace modular
