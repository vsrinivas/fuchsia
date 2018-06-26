// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_RUNNER_DEVICE_MAP_IMPL_H_
#define PERIDOT_BIN_USER_RUNNER_DEVICE_MAP_IMPL_H_

#include <map>

#include <fuchsia/ledger/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>

#include "lib/async/cpp/operation.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fidl/cpp/interface_ptr_set.h"
#include "lib/fidl/cpp/interface_request.h"
#include "peridot/lib/ledger_client/ledger_client.h"
#include "peridot/lib/ledger_client/page_client.h"
#include "peridot/lib/ledger_client/types.h"

namespace modular {

// See services/user/device_map.fidl for details.
//
// Mostly scaffolding to demonstrate a complete page client.
class DeviceMapImpl : fuchsia::modular::DeviceMap, PageClient {
 public:
  DeviceMapImpl(const std::string& device_name, const std::string& device_id,
                const std::string& device_profile, LedgerClient* ledger_client,
                LedgerPageId page_id);
  ~DeviceMapImpl() override;

  const std::string& current_device_id() const { return current_device_id_; }

  void Connect(fidl::InterfaceRequest<fuchsia::modular::DeviceMap> request);

 private:
  // |fuchsia::modular::DeviceMap|
  void Query(QueryCallback callback) override;

  // |fuchsia::modular::DeviceMap|
  void GetCurrentDevice(GetCurrentDeviceCallback callback) override;

  // |fuchsia::modular::DeviceMap|
  void SetCurrentDeviceProfile(::fidl::StringPtr profile) override;

  // |fuchsia::modular::DeviceMap|
  void WatchDeviceMap(fidl::InterfaceHandle<fuchsia::modular::DeviceMapWatcher>
                          watcher) override;

  // |PageClient|
  void OnPageChange(const std::string& key, const std::string& value) override;

  // |PageClient|
  void OnPageDelete(const std::string& key) override;

  // Update the timestamp for the current device and save it to the Ledger.
  void SaveCurrentDevice();

  // Notify all watchers that the given device has changed
  void Notify(const std::string& device_id);

  // Clients that have connected to this service.
  fidl::BindingSet<fuchsia::modular::DeviceMap> bindings_;

  // All known devices from the Ledger page.
  std::map<std::string, fuchsia::modular::DeviceMapEntry> devices_;

  // The local device in the |devices_| map.
  std::string current_device_id_;

  OperationQueue operation_queue_;

  fidl::InterfacePtrSet<fuchsia::modular::DeviceMapWatcher> change_watchers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DeviceMapImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_USER_RUNNER_DEVICE_MAP_IMPL_H_
