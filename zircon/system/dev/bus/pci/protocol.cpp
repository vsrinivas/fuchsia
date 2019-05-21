#include "device.h"

// This file contains the PciProtocol implementation contained within pci::Device
namespace pci {

zx_status_t Device::PciGetBar(uint32_t bar_id, zx_pci_bar_t* out_res) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Device::PciEnableBusMaster(bool enable) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Device::PciResetDevice() {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Device::PciMapInterrupt(zx_status_t which_irq, zx::interrupt* out_handle) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Device::PciQueryIrqMode(zx_pci_irq_mode_t mode, uint32_t* out_max_irqs) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Device::PciSetIrqMode(zx_pci_irq_mode_t mode, uint32_t requested_irq_count) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Device::PciGetDeviceInfo(zx_pcie_device_info_t* out_into) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Device::PciGetFirstCapability(uint8_t type, uint8_t* out_offset) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Device::PciConfigRead8(uint16_t offset, uint8_t* out_value) {
	return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Device::PciConfigRead16(uint16_t offset, uint16_t* out_value) {
	return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Device::PciConfigRead32(uint16_t offset, uint32_t* out_value) {
	return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Device::PciConfigWrite8(uint16_t offset, uint8_t value) {
	return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Device::PciConfigWrite16(uint16_t offset, uint16_t value) {
	return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Device::PciConfigWrite32(uint16_t offset, uint32_t value) {
	return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Device::Device::PciGetNextCapability(uint8_t type, uint8_t offset,
                                                 uint8_t* out_offset) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Device::PciGetAuxdata(const char* args,
                                  void* out_data_buffer,
                                  size_t data_size,
                                  size_t* out_data_actual) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Device::PciGetBti(uint32_t index, zx::bti* out_bti) {
    return ZX_ERR_NOT_SUPPORTED;
}

} // namespace pci
