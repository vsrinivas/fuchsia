// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/types.h>
#include <fbl/canary.h>
#include <fbl/mutex.h>
#include <object/interrupt_dispatcher.h>
#include <sys/types.h>

class InterruptEventDispatcher final : public InterruptDispatcher {
public:
    static zx_status_t Create(uint32_t vector,
                              uint32_t flags,
                              fbl::RefPtr<Dispatcher>* dispatcher,
                              zx_rights_t* rights);

    InterruptEventDispatcher(const InterruptDispatcher &) = delete;
    InterruptEventDispatcher& operator=(const InterruptDispatcher &) = delete;

    ~InterruptEventDispatcher() final;
    zx_status_t InterruptComplete() final;
    zx_status_t UserSignal() final;

private:
    explicit InterruptEventDispatcher(uint32_t vector)
            : vector_(vector),
              handler_registered_(false) {}

    static enum handler_return IrqHandler(void* ctx);

    fbl::Canary<fbl::magic("INED")> canary_;
    const uint32_t vector_;
    bool handler_registered_;
};
