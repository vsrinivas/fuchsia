// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_DEVELOPMENT_SERVICE_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_DEVELOPMENT_SERVICE_H_

#include <fidl/fuchsia.driver.development/cpp/wire.h>

#include "fidl/fuchsia.driver.development/cpp/markers.h"
#include "src/devices/bin/driver_manager/driver_runner.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"

namespace driver_manager {

class DriverDevelopmentService
    : public fidl::WireServer<fuchsia_driver_development::DriverDevelopment> {
 public:
  explicit DriverDevelopmentService(DriverRunner& driver_runner, async_dispatcher_t* dispatcher);

  zx::status<> Publish(const fbl::RefPtr<fs::PseudoDir>& svc_dir);

 private:
  // fidl::WireServer<fuchsia_driver_development::DriverDevelopmentService>
  void RestartDriverHosts(RestartDriverHostsRequestView request,
                          RestartDriverHostsCompleter::Sync& completer) override;
  void GetDriverInfo(GetDriverInfoRequestView request,
                     GetDriverInfoCompleter::Sync& completer) override;
  void GetDeviceInfo(GetDeviceInfoRequestView request,
                     GetDeviceInfoCompleter::Sync& completer) override;
  void BindAllUnboundNodes(BindAllUnboundNodesRequestView request,
                           BindAllUnboundNodesCompleter::Sync& completer) override;

  DriverRunner& driver_runner_;
  async_dispatcher_t* const dispatcher_;
};

}  // namespace driver_manager

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_DEVELOPMENT_SERVICE_H_
