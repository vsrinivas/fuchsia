#include "pci-fidl.h"

#include <lib/device-protocol/pci.h>

struct iwl_pci_fidl {
  std::unique_ptr<ddk::Pci> pci;
};

void iwl_pci_ack_interrupt(const struct iwl_pci_fidl* fidl) { fidl->pci->AckInterrupt(); }

zx_status_t iwl_pci_read_config16(const struct iwl_pci_fidl* fidl, uint16_t offset,
                                  uint16_t* out_value) {
  return fidl->pci->ReadConfig16(offset, out_value);
}

zx_status_t iwl_pci_get_device_info(const struct iwl_pci_fidl* fidl, pci_device_info_t* out_info) {
  fuchsia_hardware_pci::wire::DeviceInfo info;
  zx_status_t status = fidl->pci->GetDeviceInfo(&info);
  if (status == ZX_OK) {
    *out_info = ddk::convert_device_info_to_banjo(info);
  }
  return status;
}

zx_status_t iwl_pci_get_bti(const struct iwl_pci_fidl* fidl, uint32_t index, zx_handle_t* out_bti) {
  zx::bti bti;
  zx_status_t status = fidl->pci->GetBti(index, &bti);
  if (status == ZX_OK) {
    *out_bti = bti.release();
  }
  return status;
}

void iwl_pci_get_interrupt_modes(const struct iwl_pci_fidl* fidl,
                                 pci_interrupt_modes_t* out_modes) {
  fuchsia_hardware_pci::wire::InterruptModes modes;
  fidl->pci->GetInterruptModes(&modes);
  *out_modes = ddk::convert_interrupt_modes_to_banjo(modes);
}

zx_status_t iwl_pci_set_interrupt_mode(const struct iwl_pci_fidl* fidl, pci_interrupt_mode_t mode,
                                       uint32_t requested_irq_count) {
  return fidl->pci->SetInterruptMode(fuchsia_hardware_pci::InterruptMode{mode},
                                     requested_irq_count);
}

zx_status_t iwl_pci_set_bus_mastering(const struct iwl_pci_fidl* fidl, bool enabled) {
  return fidl->pci->SetBusMastering(enabled);
}

zx_status_t iwl_pci_map_interrupt(const struct iwl_pci_fidl* fidl, uint32_t which_irq,
                                  zx_handle_t* out_interrupt) {
  zx::interrupt interrupt;
  zx_status_t status = fidl->pci->MapInterrupt(which_irq, &interrupt);
  if (status == ZX_OK) {
    *out_interrupt = interrupt.release();
  }
  return status;
}

zx_status_t iwl_pci_write_config8(const struct iwl_pci_fidl* fidl, uint16_t offset, uint8_t value) {
  return fidl->pci->WriteConfig8(offset, value);
}

zx_status_t iwl_pci_map_bar_buffer(const struct iwl_pci_fidl* fidl, uint32_t bar_id,
                                   uint32_t cache_policy, mmio_buffer_t* buffer) {
  return fidl->pci->MapMmio(bar_id, cache_policy, buffer);
}

zx_status_t iwl_pci_connect_fragment_protocol(struct zx_device* parent, const char* fragment_name,
                                              struct iwl_pci_fidl** fidl) {
  (*fidl) = new struct iwl_pci_fidl;
  (*fidl)->pci = std::make_unique<ddk::Pci>(parent, fragment_name);

  if (!(*fidl)->pci->is_valid()) {
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

void iwl_pci_free(struct iwl_pci_fidl* fidl) {
  fidl->pci.reset();
  delete fidl;
}
