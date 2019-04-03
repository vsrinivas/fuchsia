// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <ddktl/protocol/pciroot.h>
#include <lib/fake_ddk/fake_ddk.h>

// This FakePciroot class for the moment is a stub and test files
// will specialize the methods they need. Eventually when more tests
// are sorted out it may make sense to have pciroot tests be similar
// to the mock-i2c style fakes.
class FakePciroot : public ddk::PcirootProtocol<FakePciroot> {
public:
    FakePciroot() : proto_({&pciroot_protocol_ops_, this}) {}

    const pciroot_protocol_t* proto() const { return &proto_; }

    virtual zx_status_t PcirootGetAuxdata(const char* args, void* out_data, size_t data_size,
                                          size_t* out_data_actual) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    virtual zx_status_t PcirootGetBti(uint32_t bdf, uint32_t index, zx::bti* bti) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    virtual zx_status_t PcirootConnectSysmem(zx::handle handle) { return ZX_ERR_NOT_SUPPORTED; }
    virtual zx_status_t PcirootGetPciPlatformInfo(pci_platform_info_t* info) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    virtual zx_status_t PcirootGetPciIrqInfo(pci_irq_info_t* info) { return ZX_ERR_NOT_SUPPORTED; }
    virtual bool PcirootDriverShouldProxyConfig(void) { return false; }
    virtual zx_status_t PcirootConfigRead8(const pci_bdf_t* address, uint16_t offset,
                                           uint8_t* value) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    virtual zx_status_t PcirootConfigRead16(const pci_bdf_t* address, uint16_t offset,
                                            uint16_t* value) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    virtual zx_status_t PcirootConfigRead32(const pci_bdf_t* address, uint16_t offset,
                                            uint32_t* value) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    virtual zx_status_t PcirootConfigWrite8(const pci_bdf_t* address, uint16_t offset,
                                            uint8_t value) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    virtual zx_status_t PcirootConfigWrite16(const pci_bdf_t* address, uint16_t offset,
                                             uint16_t value) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    virtual zx_status_t PcirootConfigWrite32(const pci_bdf_t* address, uint16_t offset,
                                             uint32_t value) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    virtual zx_status_t PcirootAllocMsiBlock(uint64_t requested_irqs, bool can_target_64bit,
                                             msi_block_t* out_block) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    virtual zx_status_t PcirootFreeMsiBlock(const msi_block_t* block) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    virtual zx_status_t PcirootMaskUnmaskMsi(uint64_t msi_id, bool mask) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    virtual zx_status_t PcirootGetAddressSpace(zx_paddr_t in_base, size_t len,
                                               pci_address_space_t type, bool low,
                                               uint64_t* out_base, zx::resource* resource) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    virtual zx_status_t PcirootFreeAddressSpace(uint64_t base, size_t len,
                                                pci_address_space_t type) {
        return ZX_ERR_NOT_SUPPORTED;
    }

private:
    pciroot_protocol_t proto_;
};
