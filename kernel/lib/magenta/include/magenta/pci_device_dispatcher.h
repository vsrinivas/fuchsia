// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once
#if WITH_DEV_PCIE

#include <dev/pci_common.h>
#include <dev/pcie_device.h>
#include <kernel/spinlock.h>
#include <kernel/vm/vm_aspace.h>
#include <magenta/dispatcher.h>
#include <magenta/io_mapping_dispatcher.h>
#include <magenta/syscalls/pci.h>
#include <mxtl/canary.h>
#include <mxtl/intrusive_single_list.h>
#include <mxtl/ref_counted.h>
#include <mxtl/ref_ptr.h>
#include <sys/types.h>

class PciInterruptDispatcher;

class PciDeviceDispatcher final : public Dispatcher {
public:
    static status_t Create(uint32_t index,
                           mx_pcie_device_info_t*    out_info,
                           mxtl::RefPtr<Dispatcher>* out_dispatcher,
                           mx_rights_t* out_rights);

    ~PciDeviceDispatcher() final;
    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_PCI_DEVICE; }
    const mxtl::RefPtr<PcieDevice>& device() { return device_; }

    void ReleaseDevice();

    // TODO(cja): revisit Enable____ methods to be automatic when vmos are handed
    // out so there is less of a dispatcher surface to worry about.
    status_t EnableBusMaster(bool enable);
    status_t EnableMmio(bool enable);
    status_t EnablePio(bool enable);
    const pcie_bar_info_t* GetBar(uint32_t bar_num);
    status_t GetConfig(pci_config_info_t* out);
    status_t ResetDevice();
    status_t MapInterrupt(int32_t which_irq,
                          mxtl::RefPtr<Dispatcher>* interrupt_dispatcher,
                          mx_rights_t* rights);
    status_t QueryIrqModeCaps(mx_pci_irq_mode_t mode, uint32_t* out_max_irqs);
    status_t SetIrqMode(mx_pci_irq_mode_t mode, uint32_t requested_irq_count);

    bool irqs_maskable() const TA_REQ(lock_) { return irqs_maskable_; }

private:
    PciDeviceDispatcher(mxtl::RefPtr<PcieDevice> device,
                        mx_pcie_device_info_t* out_info);

    PciDeviceDispatcher(const PciDeviceDispatcher &) = delete;
    PciDeviceDispatcher& operator=(const PciDeviceDispatcher &) = delete;

    mxtl::Canary<mxtl::magic("PCID")> canary_;

    // Lock protecting upward facing APIs.  Generally speaking, this lock is
    // held for the duration of most of our dispatcher API implementations.  It
    // is unsafe to ever attempt to acquire this lock during a callback from the
    // PCI bus driver level.
    Mutex lock_;
    mxtl::RefPtr<PcieDevice> device_ TA_GUARDED(lock_);

    uint irqs_avail_cnt_  TA_GUARDED(lock_) = 0;
    bool irqs_maskable_   TA_GUARDED(lock_) = false;
};

#endif  // if WITH_DEV_PCIE
