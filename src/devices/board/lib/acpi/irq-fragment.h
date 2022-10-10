// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_LIB_ACPI_IRQ_FRAGMENT_H_
#define SRC_DEVICES_BOARD_LIB_ACPI_IRQ_FRAGMENT_H_

#include <fidl/fuchsia.hardware.interrupt/cpp/wire.h>
#include <lib/sys/component/cpp/outgoing_directory.h>

#include <ddktl/device.h>

namespace acpi {
class Device;
class DeviceBuilder;

class IrqFragment;
using IrqFragmentDeviceType = ddk::Device<IrqFragment>;
class IrqFragment : public IrqFragmentDeviceType,
                    public fidl::WireServer<fuchsia_hardware_interrupt::Provider> {
 public:
  static zx::status<> Create(async_dispatcher_t* dispatcher, acpi::Device& parent,
                             uint32_t irq_index, uint32_t acpi_device_id);

  zx::status<> Init(uint32_t device_id);

  void DdkRelease() { delete this; }

  void Get(GetCompleter::Sync& completer) override;

 private:
  IrqFragment(async_dispatcher_t* dispatcher, acpi::Device& parent, uint32_t irq_index);

  acpi::Device& device_;
  uint32_t irq_index_;
  async_dispatcher_t* dispatcher_;

  component::OutgoingDirectory outgoing_;
};

}  // namespace acpi

#endif  // SRC_DEVICES_BOARD_LIB_ACPI_IRQ_FRAGMENT_H_
