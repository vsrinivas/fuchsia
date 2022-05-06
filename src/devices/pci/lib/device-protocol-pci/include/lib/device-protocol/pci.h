// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_PCI_LIB_DEVICE_PROTOCOL_PCI_INCLUDE_LIB_DEVICE_PROTOCOL_PCI_H_
#define SRC_DEVICES_PCI_LIB_DEVICE_PROTOCOL_PCI_INCLUDE_LIB_DEVICE_PROTOCOL_PCI_H_

#include <fuchsia/hardware/pci/c/banjo.h>
#include <lib/mmio/mmio-buffer.h>
#include <stdio.h>
#include <zircon/syscalls.h>

__BEGIN_CDECLS

zx_status_t pci_configure_interrupt_mode(const pci_protocol_t* pci, uint32_t requested_irq_count,
                                         pci_interrupt_mode_t* out_mode);
zx_status_t pci_map_bar_buffer(const pci_protocol_t* pci, uint32_t bar_id, uint32_t cache_policy,
                               mmio_buffer_t* buffer);

__END_CDECLS

#ifdef __cplusplus

#include <fuchsia/hardware/pci/cpp/banjo.h>

#include <optional>

namespace fdf {
class MmioBuffer;
}

namespace ddk {

class Pci : public ddk::PciProtocolClient {
 public:
  static constexpr char kFragmentName[] = "pci";
  Pci() = default;

  explicit Pci(const pci_protocol_t& proto) : ddk::PciProtocolClient(&proto) {}

  explicit Pci(zx_device_t* parent) : ddk::PciProtocolClient(parent) {}

  // Prefer Pci::FromFragment(parent) to construct.
  Pci(zx_device_t* parent, const char* fragment_name)
      : ddk::PciProtocolClient(parent, fragment_name) {}

  // Check Pci.is_valid() (on PriProtocolClient base class) after calling to check for proper
  // initialization. This can fail if the composite device does not expose the "pci" interface.
  static Pci FromFragment(zx_device_t* parent) { return Pci(parent, kFragmentName); }

  ~Pci() = default;

  // This class extends from the Banjo-generated ddk::PciProtocolClient which contains the client
  // implementation for the fuchsia.hardware.pci.Pci protocol.

  zx_status_t ConfigureInterruptMode(uint32_t requested_irq_count, pci_interrupt_mode_t* out_mode);
  zx_status_t MapMmio(uint32_t bar_id, uint32_t cache_policy, std::optional<fdf::MmioBuffer>* mmio);
};

}  // namespace ddk

#endif

#endif  // SRC_DEVICES_PCI_LIB_DEVICE_PROTOCOL_PCI_INCLUDE_LIB_DEVICE_PROTOCOL_PCI_H_
