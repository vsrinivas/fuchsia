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
#include <vm/vm_aspace.h>
#include <zircon/syscalls/pci.h>
#include <zircon/types.h>
#include <fbl/canary.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <object/dispatcher.h>
#include <sys/types.h>

class PciInterruptDispatcher;

class PciDeviceDispatcher final : public SoloDispatcher {
public:
    static zx_status_t Create(uint32_t index,
                              zx_pcie_device_info_t*    out_info,
                              fbl::RefPtr<Dispatcher>* out_dispatcher,
                              zx_rights_t* out_rights);

    ~PciDeviceDispatcher() final;
    zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_PCI_DEVICE; }
    const fbl::RefPtr<PcieDevice>& device() { return device_; }

    void ReleaseDevice();

    // TODO(cja): revisit Enable____ methods to be automatic when vmos are handed
    // out so there is less of a dispatcher surface to worry about.
    zx_status_t EnableBusMaster(bool enable);
    zx_status_t EnableMmio(bool enable);
    zx_status_t EnablePio(bool enable);
    const pcie_bar_info_t* GetBar(uint32_t bar_num);
    zx_status_t GetConfig(pci_config_info_t* out);
    zx_status_t ResetDevice();
    zx_status_t MapInterrupt(int32_t which_irq,
                             fbl::RefPtr<Dispatcher>* interrupt_dispatcher,
                             zx_rights_t* rights);
    zx_status_t QueryIrqModeCaps(zx_pci_irq_mode_t mode, uint32_t* out_max_irqs);
    zx_status_t SetIrqMode(zx_pci_irq_mode_t mode, uint32_t requested_irq_count);

    bool irqs_maskable() const TA_REQ(lock_) { return irqs_maskable_; }

private:
    PciDeviceDispatcher(fbl::RefPtr<PcieDevice> device,
                        zx_pcie_device_info_t* out_info);

    PciDeviceDispatcher(const PciDeviceDispatcher &) = delete;
    PciDeviceDispatcher& operator=(const PciDeviceDispatcher &) = delete;

    fbl::Canary<fbl::magic("PCID")> canary_;

    // Lock protecting upward facing APIs.  Generally speaking, this lock is
    // held for the duration of most of our dispatcher API implementations.  It
    // is unsafe to ever attempt to acquire this lock during a callback from the
    // PCI bus driver level.
    fbl::Mutex lock_;
    fbl::RefPtr<PcieDevice> device_ TA_GUARDED(lock_);

    uint irqs_avail_cnt_  TA_GUARDED(lock_) = 0;
    bool irqs_maskable_   TA_GUARDED(lock_) = false;
};

#endif  // if WITH_DEV_PCIE
