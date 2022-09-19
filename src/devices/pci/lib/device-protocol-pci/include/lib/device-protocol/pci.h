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

#include <fidl/fuchsia.hardware.pci/cpp/wire.h>
#include <fuchsia/hardware/pci/cpp/banjo.h>

#include <optional>

namespace fdf {
class MmioBuffer;
}

namespace ddk {

// This class wraps the Banjo-generated ddk::PciProtocolClient which contains the client
// implementation for the fuchsia.hardware.pci.Pci protocol. This is temporary
// while we migrate the PCI protocol from Banjo to FIDL. Eventually this class
// will go away. See fxbug.dev/99914 for details.
class Pci {
 public:
  static constexpr char kFragmentName[] = "pci";
  Pci() = default;

  explicit Pci(zx_device_t* parent) {
    zx::channel local, remote;
    ZX_ASSERT(zx::channel::create(0, &local, &remote) == ZX_OK);
    zx_status_t status =
        device_connect_fidl_protocol(parent, "fuchsia.hardware.pci.Device", remote.release());

    if (status == ZX_OK) {
      client_ =
          fidl::WireSyncClient(fidl::ClientEnd<fuchsia_hardware_pci::Device>(std::move(local)));
    }
  }

  explicit Pci(fidl::ClientEnd<fuchsia_hardware_pci::Device> client_end) {
    client_ = fidl::WireSyncClient(std::move(client_end));
  }

  // Prefer Pci::FromFragment(parent) to construct.
  Pci(zx_device_t* parent, const char* fragment_name) {
    zx::channel local, remote;
    ZX_ASSERT(zx::channel::create(0, &local, &remote) == ZX_OK);
    zx_status_t status = device_connect_fragment_fidl_protocol(
        parent, fragment_name, fidl::DiscoverableProtocolName<fuchsia_hardware_pci::Device>,
        remote.release());

    if (status == ZX_OK) {
      client_ = fidl::WireSyncClient<fuchsia_hardware_pci::Device>(
          fidl::ClientEnd<fuchsia_hardware_pci::Device>(std::move(local)));
    }
  }

  Pci(Pci&& other) = default;
  Pci& operator=(Pci&& other) = default;

  // Check Pci.is_valid() (on the PciProtocolClient) after calling to check for proper
  // initialization. This can fail if the composite device does not expose the "pci" interface.
  static Pci FromFragment(zx_device_t* parent) { return Pci(parent, kFragmentName); }

  ~Pci() = default;

  zx_status_t GetDeviceInfo(fuchsia_hardware_pci::wire::DeviceInfo* out_info) const;
  // The arena backs the memory of the Bar result and must have the same
  // lifetime or longer.
  zx_status_t GetBar(fidl::AnyArena& arena, uint32_t bar_id,
                     fuchsia_hardware_pci::wire::Bar* out_result) const;
  zx_status_t SetBusMastering(bool enabled) const;
  zx_status_t ResetDevice() const;
  zx_status_t AckInterrupt() const;
  zx_status_t MapInterrupt(uint32_t which_irq, zx::interrupt* out_interrupt) const;
  void GetInterruptModes(fuchsia_hardware_pci::wire::InterruptModes* out_modes) const;
  zx_status_t SetInterruptMode(fuchsia_hardware_pci::InterruptMode mode,
                               uint32_t requested_irq_count) const;
  zx_status_t ReadConfig8(uint16_t offset, uint8_t* out_value) const;
  zx_status_t ReadConfig16(uint16_t offset, uint16_t* out_value) const;
  zx_status_t ReadConfig32(uint16_t offset, uint32_t* out_value) const;
  zx_status_t WriteConfig8(uint16_t offset, uint8_t value) const;
  zx_status_t WriteConfig16(uint16_t offset, uint16_t value) const;
  zx_status_t WriteConfig32(uint16_t offset, uint32_t value) const;
  zx_status_t GetFirstCapability(fuchsia_hardware_pci::CapabilityId id, uint8_t* out_offset) const;
  zx_status_t GetNextCapability(fuchsia_hardware_pci::CapabilityId id, uint8_t start_offset,
                                uint8_t* out_offset) const;
  zx_status_t GetFirstExtendedCapability(fuchsia_hardware_pci::ExtendedCapabilityId id,
                                         uint16_t* out_offset) const;
  zx_status_t GetNextExtendedCapability(fuchsia_hardware_pci::ExtendedCapabilityId id,
                                        uint16_t start_offset, uint16_t* out_offset) const;
  zx_status_t GetBti(uint32_t index, zx::bti* out_bti) const;

  // These two methods are not Banjo methods but miscellaneous PCI helper
  // methods.
  zx_status_t ConfigureInterruptMode(uint32_t requested_irq_count,
                                     fuchsia_hardware_pci::InterruptMode* out_mode) const;
  zx_status_t MapMmio(uint32_t bar_id, uint32_t cache_policy,
                      std::optional<fdf::MmioBuffer>* mmio) const;

  // This overload is provided for backwards-compatibility; raw mmio_buffer_t
  // objects should not be used in new C++ code. Instead, use the
  // fdf::MmioBuffer wrapper class.
  zx_status_t MapMmio(uint32_t bar_id, uint32_t cache_policy, mmio_buffer_t* mmio) const;

  bool is_valid() const { return client_.is_valid(); }

 private:
  zx_status_t MapMmioInternal(uint32_t bar_Id, uint32_t cache_policy, zx::vmo* out_vmo) const;

  fidl::WireSyncClient<fuchsia_hardware_pci::Device> client_;
};

// Some helper functions to convert FIDL types to Banjo, for use mainly by C
// drivers that can't directly use the C++ types.
pci_device_info_t convert_device_info_to_banjo(const fuchsia_hardware_pci::wire::DeviceInfo& info);
pci_interrupt_modes_t convert_interrupt_modes_to_banjo(
    const fuchsia_hardware_pci::wire::InterruptModes& modes);

// The pci_bar_t object takes ownership of the Bar's handles.
pci_bar_t convert_bar_to_banjo(fuchsia_hardware_pci::wire::Bar bar);

}  // namespace ddk

#endif

#endif  // SRC_DEVICES_PCI_LIB_DEVICE_PROTOCOL_PCI_INCLUDE_LIB_DEVICE_PROTOCOL_PCI_H_
