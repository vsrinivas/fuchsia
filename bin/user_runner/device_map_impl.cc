// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/user_runner/device_map_impl.h"

#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/fidl/json_xdr.h"
#include "apps/modular/lib/fidl/page_snapshot.h"
#include "apps/modular/lib/rapidjson/rapidjson.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/ftl/time/time_point.h"
#include "lib/mtl/vmo/strings.h"

namespace modular {

// Prefix of the keys under which device entries are stored in the
// user root page. After the prefix follows the device ID.
constexpr char kDeviceKeyPrefix[] = "Device/";

namespace {

bool IsDeviceKey(const fidl::Array<uint8_t>& key) {
  constexpr size_t prefix_size = sizeof(kDeviceKeyPrefix) - 1;

  // NOTE(mesch): A key that is *only* the prefix, without anything
  // after it, is still not a valid story key. So the key must be
  // truly longer than the prefix.
  return key.size() > prefix_size &&
         0 == memcmp(key.data(), kDeviceKeyPrefix, prefix_size);
}

void WriteDeviceName(std::string device_name, ledger::Page* const page) {
  std::string json;
  XdrWrite(&json, &device_name, XdrFilter<std::string>);

  std::string key{kDeviceKeyPrefix + device_name};
  page->Put(to_array(key), to_array(json), [](ledger::Status){});
}

}  // namespace

// Asynchronous operations of this service.

class DeviceMapImpl::QueryCall : Operation<fidl::Array<fidl::String>> {
 public:
  QueryCall(
      OperationContainer* const container,
      std::shared_ptr<ledger::PageSnapshotPtr> const snapshot,
      ResultCall result_call)
      : Operation(container, std::move(result_call)),
        snapshot_(snapshot) {
    data_.resize(0);  // never return null
    Ready();
  }

 private:
  void Run() override { GetEntries(nullptr); }

  void GetEntries(fidl::Array<uint8_t> continuation_token) {
    (*snapshot_)->GetEntries(
        to_array(kDeviceKeyPrefix), std::move(continuation_token),
        [this](ledger::Status status, fidl::Array<ledger::EntryPtr> entries,
               fidl::Array<uint8_t> continuation_token) {
          if (status != ledger::Status::OK &&
              status != ledger::Status::PARTIAL_RESULT) {
            FTL_LOG(ERROR) << "Ledger status " << status << ".";
            Done(std::move(data_));
            return;
          }

          if (entries.size() == 0) {
            // No existing entries.
            Done(std::move(data_));
            return;
          }

          for (const auto& entry : entries) {
            std::string value;
            if (!mtl::StringFromVmo(entry->value, &value)) {
              FTL_LOG(ERROR) << "VMO for key " << to_string(entry->key)
                             << " couldn't be copied.";
              continue;
            }

            std::string device_name;
            if (!XdrRead(value, &device_name, XdrFilter<std::string>)) {
              continue;
            }

            data_.push_back(std::move(device_name));
          }

          if (status == ledger::Status::PARTIAL_RESULT) {
            GetEntries(std::move(continuation_token));
          } else {
            Done(std::move(data_));
          }
        });
  }

  std::shared_ptr<ledger::PageSnapshotPtr> snapshot_;
  fidl::Array<fidl::String> data_;
  FTL_DISALLOW_COPY_AND_ASSIGN(QueryCall);
};

DeviceMapImpl::DeviceMapImpl(const std::string& device_name, ledger::Page* const page)
    : device_name_(device_name),
      page_(page),
      page_watcher_binding_(this),
      snapshot_("DeviceMapImpl") {
  page_->GetSnapshot(snapshot_.NewRequest(), page_watcher_binding_.NewBinding(),
                     [](ledger::Status status) {
                       if (status != ledger::Status::OK) {
                         FTL_LOG(ERROR) << "Page.GetSnapshot() status: " << status;
                       }
                     });

  WriteDeviceName(device_name_, page_);
}

DeviceMapImpl::~DeviceMapImpl() = default;

void DeviceMapImpl::AddBinding(fidl::InterfaceRequest<DeviceMap> request) {
  bindings_.AddBinding(this, std::move(request));
}

void DeviceMapImpl::Query(const QueryCallback& callback) {
  new QueryCall(&operation_queue_, snapshot_.shared_ptr(), callback);
}

void DeviceMapImpl::OnChange(ledger::PageChangePtr page,
                             ledger::ResultState result_state,
                             const OnChangeCallback& callback) {
  bool update = false;
  for (auto& entry : page->changes) {
    if (!IsDeviceKey(entry->key)) {
      continue;
    }

    FTL_LOG(INFO) << "New Device: " << to_string(entry->key);
    update = true;
  }

  if (update &&
      (result_state == ledger::ResultState::COMPLETED ||
       result_state == ledger::ResultState::PARTIAL_COMPLETED)) {
    callback(snapshot_.NewRequest());
  } else {
    callback(nullptr);
  }
}

}  // namespace modular
