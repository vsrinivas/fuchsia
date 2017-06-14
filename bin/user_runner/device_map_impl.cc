// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/user_runner/device_map_impl.h"

#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/fidl/json_xdr.h"
#include "apps/modular/lib/ledger/storage.h"
#include "apps/modular/lib/rapidjson/rapidjson.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/ftl/time/time_point.h"
#include "lib/mtl/vmo/strings.h"

namespace modular {

namespace {

void XdrDeviceData(XdrContext* const xdr, DeviceMapEntryPtr* const data) {
  xdr->Field("name", &data->get()->name);
  xdr->Field("device_id", &data->get()->device_id);
  xdr->Field("profile", &data->get()->profile);
}

void WriteDeviceData(const std::string& device_name,
                     const std::string& device_id,
                     const std::string& profile,
                     ledger::Page* const page) {
  std::string json;
  DeviceMapEntryPtr device = DeviceMapEntry::New();
  device->name = device_name;
  device->device_id = device_id;
  device->profile = profile;
  XdrWrite(&json, &device, XdrDeviceData);
  page->Put(to_array(MakeDeviceKey(device_id)), to_array(json),
            [](ledger::Status) {});
}

}  // namespace

// Asynchronous operations of this service.

class DeviceMapImpl::QueryCall : Operation<fidl::Array<DeviceMapEntryPtr>> {
 public:
  QueryCall(OperationContainer* const container,
            std::shared_ptr<ledger::PageSnapshotPtr> const snapshot,
            ResultCall result_call)
      : Operation(container, std::move(result_call)), snapshot_(snapshot) {
    data_.resize(0);  // never return null
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this, &data_};

    GetEntries((*snapshot_).get(), &entries_,
               [this, flow](ledger::Status status) {
                 if (status != ledger::Status::OK) {
                   FTL_LOG(ERROR) << "QueryCall() "
                                  << "GetEntries() " << status;
                   return;
                 }

                 Cont(flow);
               });
  }

  void Cont(FlowToken flow) {
    if (entries_.size() == 0) {
      // No existing entries.
      return;
    }

    for (const auto& entry : entries_) {
      std::string value;
      if (!mtl::StringFromVmo(entry->value, &value)) {
        FTL_LOG(ERROR) << "VMO for key " << to_string(entry->key)
                       << " couldn't be copied.";
        continue;
      }

      auto device = DeviceMapEntry::New();
      if (!XdrRead(value, &device, XdrDeviceData)) {
        continue;
      }

      data_.push_back(std::move(device));
    }
  }

  std::shared_ptr<ledger::PageSnapshotPtr> snapshot_;
  std::vector<ledger::EntryPtr> entries_;
  fidl::Array<DeviceMapEntryPtr> data_;
  FTL_DISALLOW_COPY_AND_ASSIGN(QueryCall);
};

DeviceMapImpl::DeviceMapImpl(const std::string& device_name,
                             const std::string& device_id,
                             const std::string& device_profile,
                             ledger::Page* const page)
    : PageClient("DeviceMapImpl", page, kDeviceKeyPrefix) {
  WriteDeviceData(device_name, device_id, device_profile, page);
}

DeviceMapImpl::~DeviceMapImpl() = default;

void DeviceMapImpl::Connect(fidl::InterfaceRequest<DeviceMap> request) {
  bindings_.AddBinding(this, std::move(request));
}

void DeviceMapImpl::Query(const QueryCallback& callback) {
  new QueryCall(&operation_queue_, page_snapshot(), callback);
}

void DeviceMapImpl::OnChange(const std::string& key, const std::string& value) {
  FTL_LOG(INFO) << "New Device: " << key;
}

void DeviceMapImpl::OnDelete(const std::string& key) {
  FTL_LOG(INFO) << "Deleted Device: " << key;
}

}  // namespace modular
