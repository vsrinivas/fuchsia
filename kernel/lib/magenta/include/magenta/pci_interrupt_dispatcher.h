// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once
#if WITH_DEV_PCIE

#include <dev/pcie_irqs.h>
#include <magenta/interrupt_dispatcher.h>
#include <magenta/pci_device_dispatcher.h>
#include <sys/types.h>

class PciDeviceDispatcher;

class PciInterruptDispatcher final : public InterruptDispatcher {
public:
    static status_t Create(const mxtl::RefPtr<PcieDevice>& device,
                           uint32_t irq_id,
                           bool maskable,
                           mx_rights_t* out_rights,
                           mxtl::RefPtr<Dispatcher>* out_interrupt);

    ~PciInterruptDispatcher() final;
    status_t InterruptComplete() final;
    status_t UserSignal() final;

private:
    static pcie_irq_handler_retval_t IrqThunk(const PcieDevice& dev,
                                              uint irq_id,
                                              void* ctx);
    PciInterruptDispatcher(uint32_t irq_id, bool maskable)
        : irq_id_(irq_id),
          maskable_(maskable) { }

    const uint32_t irq_id_;
    const bool     maskable_;
    mxtl::RefPtr<PcieDevice> device_;
};

#endif  // if WITH_DEV_PCIE
