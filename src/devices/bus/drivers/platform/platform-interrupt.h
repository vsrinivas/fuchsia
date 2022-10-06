// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BUS_DRIVERS_PLATFORM_PLATFORM_INTERRUPT_H_
#define SRC_DEVICES_BUS_DRIVERS_PLATFORM_PLATFORM_INTERRUPT_H_

#include <fidl/fuchsia.hardware.interrupt/cpp/wire_messaging.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/sys/component/cpp/outgoing_directory.h>

#include <ddktl/device.h>

namespace platform_bus {

class PlatformDevice;

class PlatformInterruptFragment;
using InterruptDeviceType = ddk::Device<PlatformInterruptFragment>;

class PlatformInterruptFragment : public InterruptDeviceType,
                                  public fidl::WireServer<fuchsia_hardware_interrupt::Provider> {
 public:
  PlatformInterruptFragment(zx_device_t* parent, PlatformDevice* pdev, uint32_t index,
                            async_dispatcher_t* dispatcher)
      : InterruptDeviceType(parent),
        pdev_(pdev),
        index_(index),
        outgoing_(component::OutgoingDirectory::Create(dispatcher)),
        dispatcher_(dispatcher) {}

  // Interrupt provider implementation.
  void Get(GetCompleter::Sync& completer);

  zx_status_t Add(const char* name, PlatformDevice* pdev, fuchsia_hardware_platform_bus::Irq& irq);

  void DdkRelease() { delete this; }

 private:
  PlatformDevice* pdev_;
  uint32_t index_;
  component::OutgoingDirectory outgoing_;
  async_dispatcher_t* dispatcher_;
};

}  // namespace platform_bus

#endif  // SRC_DEVICES_BUS_DRIVERS_PLATFORM_PLATFORM_INTERRUPT_H_
