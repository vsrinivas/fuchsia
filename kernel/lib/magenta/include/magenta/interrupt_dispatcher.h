// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <dev/interrupt_event.h>
#include <magenta/dispatcher.h>
#include <sys/types.h>

class InterruptDispatcher final : public Dispatcher {
public:
    static status_t Create(uint32_t vector, uint32_t flags, utils::RefPtr<Dispatcher>* dispatcher,
                           mx_rights_t* rights);

    virtual ~InterruptDispatcher() final;
    void Close(Handle* handle) final;

    InterruptDispatcher* get_interrupt_dispatcher() final { return this; }

    // Wait for an interrupt.
    status_t InterruptWait();
    // Notify the system that the caller has finished processing the interrupt.
    // Required before the handle can be waited upon again.
    status_t InterruptComplete();

private:
    explicit InterruptDispatcher(interrupt_event_t ie);

    interrupt_event_t interrupt_event_;
    bool signaled_;
};
