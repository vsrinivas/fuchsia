// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_ADB_BIN_ADB_SERVICE_MANAGER_H_
#define SRC_DEVELOPER_ADB_BIN_ADB_SERVICE_MANAGER_H_

#include <fidl/fuchsia.component.decl/cpp/fidl.h>
#include <fidl/fuchsia.component/cpp/fidl.h>
#include <fidl/fuchsia.hardware.adb/cpp/fidl.h>
#include <lib/sys/component/cpp/service_client.h>

namespace adb {

class ServiceManager {
 public:
  explicit ServiceManager() = default;

  zx_status_t Init();

  // Use the fuchsia.component.Realm protocol to create a dynamic
  // child instance in the collection.
  zx::status<fidl::ClientEnd<fuchsia_hardware_adb::Provider>> CreateDynamicChild(
      std::string_view name);

  // Use the fuchsia.component.Realm protocol to open the exposed directory of
  // the dynamic child instance.
  zx::status<fidl::ClientEnd<fuchsia_hardware_adb::Provider>> ConnectDynamicChild(
      std::string_view name);

 private:
  fidl::WireSyncClient<fuchsia_component::Realm> realm_proxy_;
};

}  // namespace adb

#endif  // SRC_DEVELOPER_ADB_BIN_ADB_SERVICE_MANAGER_H_
