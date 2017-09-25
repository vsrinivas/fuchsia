// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_USER_RUNNER_DEVICE_MAP_IMPL_H_
#define APPS_MODULAR_SRC_USER_RUNNER_DEVICE_MAP_IMPL_H_

#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ledger/fidl/ledger.fidl.h"
#include "lib/user/fidl/device_map.fidl.h"
#include "peridot/lib/fidl/operation.h"
#include "peridot/lib/ledger/ledger_client.h"
#include "peridot/lib/ledger/page_client.h"
#include "peridot/lib/ledger/types.h"

namespace modular {

// See services/user/device_map.fidl for details.
//
// Mostly scaffolding to demonstrate a complete page client.
class DeviceMapImpl : DeviceMap, PageClient {
 public:
  DeviceMapImpl(const std::string& device_name,
                const std::string& device_id,
                const std::string& device_profile,
                LedgerClient* ledger_client,
                LedgerPageId page_id);
  ~DeviceMapImpl() override;

  void Connect(fidl::InterfaceRequest<DeviceMap> request);

 private:
  // |DeviceMap|
  void Query(const QueryCallback& callback) override;

  // |DeviceMap|
  void GetCurrentDevice(const GetCurrentDeviceCallback& callback) override;

  // |PageClient|
  void OnPageChange(const std::string& key, const std::string& value) override;

  // |PageClient|
  void OnPageDelete(const std::string& key) override;

  fidl::BindingSet<DeviceMap> bindings_;

  DeviceMapEntry current_device_;

  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DeviceMapImpl);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_USER_RUNNER_DEVICE_MAP_H_
