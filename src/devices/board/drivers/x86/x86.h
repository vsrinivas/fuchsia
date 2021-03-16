// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_X86_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_X86_H_

#include <fuchsia/hardware/acpi/llcpp/fidl.h>
#include <fuchsia/hardware/platform/bus/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <threads.h>

#include <memory>

#include <ddktl/device.h>
#include <fbl/macros.h>
#include <fbl/vector.h>

#include "src/devices/lib/iommu/iommu.h"

namespace x86 {

// This is the main class for the X86 platform bus driver.
class X86 : public ddk::Device<X86, ddk::Messageable>,
            public fuchsia_hardware_acpi::Acpi::Interface {
 public:
  explicit X86(zx_device_t* parent, pbus_protocol_t* pbus, zx_device_t* sys_root)
      : ddk::Device<X86, ddk::Messageable>(parent), pbus_(pbus), sys_root_(sys_root) {}
  ~X86();

  static zx_status_t Create(void* ctx, zx_device_t* parent, std::unique_ptr<X86>* out);
  static zx_status_t CreateAndBind(void* ctx, zx_device_t* parent);
  static bool RunUnitTests(void* ctx, zx_device_t* parent, zx_handle_t channel);

  // Device protocol implementation.
  void DdkRelease();
  zx_status_t DdkMessage(fidl_incoming_msg*, fidl_txn*);

  // ACPI protocol FIDL interface implementation.
  void ListTableEntries(ListTableEntriesCompleter::Sync& completer) override;
  void ReadNamedTable(fidl::Array<uint8_t, 4> name, uint32_t instance, ::zx::vmo result,
                      ReadNamedTableCompleter::Sync& completer) override;

  // Performs ACPICA initialization.
  zx_status_t EarlyAcpiInit();

  zx_status_t EarlyInit();

  // Add the list of ACPI entries present in the system to |entries|.
  //
  // Requires that ACPI has been initialised.
  zx_status_t GetAcpiTableEntries(fbl::Vector<fuchsia_hardware_acpi::wire::TableInfo>* entries);

 private:
  X86(const X86&) = delete;
  X86(X86&&) = delete;
  X86& operator=(const X86&) = delete;
  X86& operator=(X86&&) = delete;

  zx_status_t SysmemInit();

  zx_status_t GoldfishControlInit();

  // Register this instance with devmgr and launch the deferred initialization in Thread.
  zx_status_t Bind();
  zx_status_t Start();
  int Thread();

  IommuManager iommu_manager_{[](fx_log_severity_t severity, const char* file, int line,
                                 const char* msg,
                                 va_list args) { zxlogvf_etc(severity, file, line, msg, args); }};

  ddk::PBusProtocolClient pbus_;

  // This is our parents parent.
  zx_device_t* sys_root_;

  thrd_t thread_;

  // Whether the global ACPICA initialization has been performed or not
  bool acpica_initialized_ = false;
};

}  // namespace x86

#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_X86_H_
