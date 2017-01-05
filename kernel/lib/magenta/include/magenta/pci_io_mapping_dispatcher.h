// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once
#if WITH_DEV_PCIE

#include <dev/pci_common.h>
#include <magenta/io_mapping_dispatcher.h>
#include <sys/types.h>

class PciDeviceDispatcher;
class PciIoMappingDispatcher final : public IoMappingDispatcher {
public:
    static status_t Create(const mxtl::RefPtr<PciDeviceDispatcher::PciDeviceWrapper>& device,
                           const char* dbg_tag,
                           paddr_t paddr,
                           size_t size,
                           uint vmm_flags,
                           uint arch_mmu_flags,
                           mxtl::RefPtr<Dispatcher>* out_dispatcher,
                           mx_rights_t* out_rights);

    static status_t CreateBarMapping(
            const mxtl::RefPtr<PciDeviceDispatcher::PciDeviceWrapper>& device,
            uint bar_num,
            uint vmm_flags,
            uint cache_policy,
            mxtl::RefPtr<Dispatcher>* out_dispatcher,
            mx_rights_t* out_rights);

    ~PciIoMappingDispatcher() final;

    // TODO(cpu): this should be removed when device waiting is refactored.
    void Close() override;

private:
    PciIoMappingDispatcher(const mxtl::RefPtr<PciDeviceDispatcher::PciDeviceWrapper>& device)
        : device_(device) { }

    mxtl::RefPtr<PciDeviceDispatcher::PciDeviceWrapper> device_;
    uint bar_num_ = PCIE_MAX_BAR_REGS;
};

#endif  // if WITH_DEV_PCIE
