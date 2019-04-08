// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <fbl/canary.h>
#include <fbl/vector.h>
#include <kernel/mp.h>
#include <object/handle.h>
#include <object/interrupt_dispatcher.h>
#include <sys/types.h>
#include <zircon/types.h>

class InterruptEventDispatcher final : public InterruptDispatcher {
public:
    static zx_status_t Create(KernelHandle<InterruptDispatcher>* handle,
                              zx_rights_t* rights,
                              uint32_t vector,
                              uint32_t options);

    ~InterruptEventDispatcher() final;

    InterruptEventDispatcher(const InterruptDispatcher &) = delete;
    InterruptEventDispatcher& operator=(const InterruptDispatcher &) = delete;

    zx_status_t BindVcpu(fbl::RefPtr<VcpuDispatcher> vcpu_dispatcher) final;

private:
    explicit InterruptEventDispatcher(uint32_t vector);

    void MaskInterrupt() final;
    void UnmaskInterrupt() final;
    void UnregisterInterruptHandler() final;
    bool HasVcpu() const final;

    zx_status_t RegisterInterruptHandler();
    static interrupt_eoi IrqHandler(void* ctx);
    static interrupt_eoi VcpuIrqHandler(void* ctx);
    void VcpuInterruptHandler();

    const uint32_t vector_;
    fbl::Vector<fbl::RefPtr<VcpuDispatcher>> vcpus_;
};
