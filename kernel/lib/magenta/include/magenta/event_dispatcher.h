// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/event.h>

#include <magenta/dispatcher.h>
#include <magenta/waiter.h>

#include <sys/types.h>

class EventDispatcher final : public Dispatcher {
public:
    static status_t Create(uint32_t options, utils::RefPtr<Dispatcher>* dispatcher,
                           mx_rights_t* rights);

    ~EventDispatcher() final;
    void Close(Handle* handle) final;
    EventDispatcher* get_event_dispatcher() final { return this; }


    Waiter* BeginWait(event_t* event, Handle* handle, mx_signals_t signals);

    status_t SignalEvent();

    status_t ResetEvent();

    status_t UserSignal(uint32_t set_mask, uint32_t clear_mask) final;

private:
    explicit EventDispatcher(uint32_t options);
    Waiter waiter_;
};
