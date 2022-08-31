// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_V2_DRIVER_DEVELOPMENT_SERVICE_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_V2_DRIVER_DEVELOPMENT_SERVICE_H_

#include <fidl/fuchsia.driver.development/cpp/wire.h>
#include <lib/sys/component/cpp/outgoing_directory.h>

#include "fidl/fuchsia.driver.development/cpp/markers.h"
#include "src/devices/bin/driver_manager/v2/driver_runner.h"

namespace driver_manager {

class DriverDevelopmentService
    : public fidl::WireServer<fuchsia_driver_development::DriverDevelopment> {
 public:
  explicit DriverDevelopmentService(dfv2::DriverRunner& driver_runner,
                                    async_dispatcher_t* dispatcher);

  void Publish(component::OutgoingDirectory& outgoing);

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
  void IsDfv2(IsDfv2RequestView request, IsDfv2Completer::Sync& completer) override;
  void AddTestNode(AddTestNodeRequestView request, AddTestNodeCompleter::Sync& completer) override;
  void RemoveTestNode(RemoveTestNodeRequestView request,
                      RemoveTestNodeCompleter::Sync& completer) override;

  dfv2::DriverRunner& driver_runner_;
  // A map of the test nodes that have been created.
  std::map<std::string, std::weak_ptr<dfv2::Node>> test_nodes_;
  async_dispatcher_t* const dispatcher_;
};

}  // namespace driver_manager

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_V2_DRIVER_DEVELOPMENT_SERVICE_H_
