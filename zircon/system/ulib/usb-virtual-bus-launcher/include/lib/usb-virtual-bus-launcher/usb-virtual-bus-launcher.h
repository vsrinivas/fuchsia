// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_USB_VIRTUAL_BUS_LAUNCHER_USB_VIRTUAL_BUS_LAUNCHER_H_
#define LIB_USB_VIRTUAL_BUS_LAUNCHER_USB_VIRTUAL_BUS_LAUNCHER_H_

#include <fidl/fuchsia.hardware.usb.peripheral/cpp/wire.h>
#include <fidl/fuchsia.hardware.usb.virtual.bus/cpp/wire.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/usb-virtual-bus-launcher-helper/usb-virtual-bus-launcher-helper.h>
#include <lib/zx/status.h>

#include <vector>

namespace usb_virtual {

using driver_integration_test::IsolatedDevmgr;

using ConfigurationDescriptor =
    ::fidl::VectorView<fuchsia_hardware_usb_peripheral::wire::FunctionDescriptor>;
using fuchsia_hardware_usb_peripheral::wire::DeviceDescriptor;

// Helper class that launches an isolated device manager with a virtual USB bus for tests.
class BusLauncher {
 public:
  BusLauncher(BusLauncher&& other) = default;
  BusLauncher& operator=(BusLauncher&& other) = default;

  BusLauncher(const BusLauncher&) = delete;
  BusLauncher& operator=(const BusLauncher&) = delete;

  // Create the isolated device manager, wait for it to start, then enable the virtual USB bus.
  // Optionally takes |args| to passed to IsolatedDevmgr. This can be used to enable logging for
  // your driver under test. for example:
  //
  //   IsolatedDevmgr::Args args = {
  //     .log_level =
  //       {
  //          DriverLog{
  //            .name = "driver_name",
  //            .log_level = Severity::DEBUG,
  //          },
  //       }
  //   };
  //   usb_virtual::Bus::Create(std::move(args));
  static zx::status<BusLauncher> Create(IsolatedDevmgr::Args args = {});

  // Set up a USB peripheral device with the given descriptors. See fuchsia.hardware.usb.peripheral
  // for more information. Waits for the functions to be registered and triggers a connect event on
  // the virtual bus.
  [[nodiscard]] zx_status_t SetupPeripheralDevice(
      DeviceDescriptor&& device_desc, std::vector<ConfigurationDescriptor> config_descs);

  // Asks the peripheral device to clear its functions and waits for the FunctionsCleared event.
  [[nodiscard]] zx_status_t ClearPeripheralDeviceFunctions();

  // Get a file descriptor to the root of the isolate device manager's devfs.
  int GetRootFd();

  // Disable the virtual bus.
  [[nodiscard]] zx_status_t Disable();

  // Disconnect the virtual bus.
  [[nodiscard]] zx_status_t Disconnect();

 private:
  BusLauncher() = default;

  IsolatedDevmgr devmgr_;
  fidl::WireSyncClient<fuchsia_hardware_usb_peripheral::Device> peripheral_;
  fidl::WireSyncClient<fuchsia_hardware_usb_virtual_bus::Bus> virtual_bus_;
};

}  // namespace usb_virtual

#endif  // LIB_USB_VIRTUAL_BUS_LAUNCHER_USB_VIRTUAL_BUS_LAUNCHER_H_
