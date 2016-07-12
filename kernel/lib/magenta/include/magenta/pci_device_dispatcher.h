// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <dev/pcie.h>
#include <dev/pcie_constants.h>
#include <kernel/spinlock.h>
#include <kernel/vm/vm_aspace.h>
#include <magenta/dispatcher.h>
#include <magenta/io_mapping_dispatcher.h>
#include <sys/types.h>
#include <utils/intrusive_single_list.h>

class PciInterruptDispatcher;

class PciDeviceDispatcher final : public Dispatcher {
public:
    class PciDeviceWrapper final : public utils::RefCounted<PciDeviceWrapper> {
      public:
        static status_t Create(uint32_t index,
                               utils::RefPtr<PciDeviceWrapper>* out_device);

        status_t Claim();   // Called only from PciDeviceDispatcher while holding dispatcher lock_

        status_t AddBarCachePolicyRef(uint bar_num, uint32_t cache_policy);
        void ReleaseBarCachePolicyRef(uint bar_num);

        pcie_device_state_t* device() const { return device_; }
        bool claimed() const { return claimed_; }

      private:
        friend class utils::RefPtr<PciDeviceWrapper>;

        struct CachePolicyRef {
            uint     ref_count;
            uint32_t cache_policy;
        };

        explicit PciDeviceWrapper(pcie_device_state_t* device);
        PciDeviceWrapper(const PciDeviceWrapper &) = delete;
        PciDeviceWrapper& operator=(const PciDeviceWrapper &) = delete;
        ~PciDeviceWrapper();

        mutex_t              cp_ref_lock_;
        CachePolicyRef       cp_refs_[PCIE_MAX_BAR_REGS];
        pcie_device_state_t* device_;
        bool                 claimed_ = false;
    };

    static status_t Create(uint32_t                   index,
                           mx_pcie_get_nth_info_t*    out_info,
                           utils::RefPtr<Dispatcher>* out_dispatcher,
                           mx_rights_t*               out_rights);

    ~PciDeviceDispatcher() final;
    mx_obj_type_t GetType() const final { return MX_OBJ_TYPE_PCI_DEVICE; }
    PciDeviceDispatcher* get_pci_device_dispatcher() final { return this; }

    void ReleaseDevice();

    status_t ClaimDevice();
    status_t EnableBusMaster(bool enable);
    status_t ResetDevice();
    status_t MapConfig(utils::RefPtr<Dispatcher>* out_mapping,
                       mx_rights_t* out_rights);
    status_t MapMmio(uint32_t bar_num,
                     uint32_t cache_policy,
                     utils::RefPtr<Dispatcher>* out_mapping,
                     mx_rights_t* out_rights);
    status_t MapInterrupt(int32_t which_irq,
                          utils::RefPtr<Dispatcher>* interrupt_dispatcher,
                          mx_rights_t* rights);
    status_t QueryIrqModeCaps(mx_pci_irq_mode_t mode, uint32_t* out_max_irqs);
    status_t SetIrqMode(mx_pci_irq_mode_t mode, uint32_t requested_irq_count);

    bool irqs_maskable() const { return irqs_maskable_; }

private:
    PciDeviceDispatcher(utils::RefPtr<PciDeviceWrapper> device,
                        mx_pcie_get_nth_info_t* out_info);

    PciDeviceDispatcher(const PciDeviceDispatcher &) = delete;
    PciDeviceDispatcher& operator=(const PciDeviceDispatcher &) = delete;

    // Lock protecting upward facing APIs.  Generally speaking, this lock is
    // held for the duration of most of our dispatcher API implementations.  It
    // is unsafe to ever attempt to acquire this lock during a callback from the
    // PCI bus driver level.
    mutex_t lock_;
    utils::RefPtr<PciDeviceWrapper> device_;

    uint irqs_supported_ = 0;
    bool irqs_maskable_  = false;
};
