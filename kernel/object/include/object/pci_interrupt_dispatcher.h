// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once
#if WITH_DEV_PCIE

#include <fbl/canary.h>
#include <object/interrupt_dispatcher.h>
#include <object/pci_device_dispatcher.h>
#include <sys/types.h>

class PciDeviceDispatcher;

class PciInterruptDispatcher final : public InterruptDispatcher {
public:
    static zx_status_t Create(const fbl::RefPtr<PcieDevice>& device,
                              uint32_t irq_id,
                              bool maskable,
                              zx_rights_t* out_rights,
                              fbl::RefPtr<Dispatcher>* out_interrupt);

    ~PciInterruptDispatcher() final;

    zx_status_t Bind(uint32_t slot, uint32_t vector, uint32_t options) final;

protected:
    void MaskInterrupt(uint32_t vector) final;
    void UnmaskInterrupt(uint32_t vector) final;
    zx_status_t RegisterInterruptHandler(uint32_t vector, void* data) final;
    void UnregisterInterruptHandler(uint32_t vector) final;

private:
    static pcie_irq_handler_retval_t IrqThunk(const PcieDevice& dev,
                                              uint irq_id,
                                              void* ctx);
    PciInterruptDispatcher(const fbl::RefPtr<PcieDevice>& device, bool maskable)
        : device_(device),
          maskable_(maskable) { }

    fbl::Canary<fbl::magic("INPD")> canary_;

    fbl::RefPtr<PcieDevice> device_;
    const bool maskable_;
};

#endif  // if WITH_DEV_PCIE
