// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/types.h>
#include <fbl/canary.h>
#include <object/interrupt_dispatcher.h>
#include <sys/types.h>

class InterruptEventDispatcher final : public InterruptDispatcher {
public:
    static zx_status_t Create(fbl::RefPtr<Dispatcher>* dispatcher,
                              zx_rights_t* rights);

    InterruptEventDispatcher(const InterruptDispatcher &) = delete;
    InterruptEventDispatcher& operator=(const InterruptDispatcher &) = delete;

    zx_status_t Bind(uint32_t slot, uint32_t vector, uint32_t options) final;

protected:
    void MaskInterrupt(uint32_t vector) final;
    void UnmaskInterrupt(uint32_t vector) final;
    zx_status_t RegisterInterruptHandler(uint32_t vector, void* data) final;
    void UnregisterInterruptHandler(uint32_t vector) final;

private:
    explicit InterruptEventDispatcher() {}

    static void IrqHandler(void* ctx);

    fbl::Canary<fbl::magic("INED")> canary_;
};
