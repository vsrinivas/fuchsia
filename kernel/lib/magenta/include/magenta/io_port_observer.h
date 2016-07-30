// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <magenta/dispatcher.h>
#include <magenta/state_observer.h>
#include <magenta/types.h>

#include <utils/ref_ptr.h>

class IOPortDispatcher;

class IOPortObserver final: public StateObserver {
public:
    enum {
        NEW,          // Initial state, it transitions to either CANCELLED or UNBOUND.
        CANCELLED,
        UNBOUND
    };

    IOPortObserver(utils::RefPtr<IOPortDispatcher> io_port,
                   Handle* handle,
                   mx_signals_t watched_signals,
                   uint64_t key);

    ~IOPortObserver() = default;

    Handle* get_handle() const { return handle_; }
    uint64_t get_key() const { return key_; }

    int GetState();
    int SetState(int new_state);

private:
    IOPortObserver() = delete;
    IOPortObserver(const IOPortObserver&) = delete;
    IOPortObserver& operator=(const IOPortObserver&) = delete;

    // StateObserver implementation:
    bool OnInitialize(mx_signals_state_t initial_state) final;
    bool OnStateChange(mx_signals_state_t new_state) final;
    bool OnCancel(Handle* handle, bool* should_remove, bool* call_did_cancel) final;
    void OnDidCancel() final;

    bool MaybeSignal(mx_signals_state_t state);

    volatile int state_;

    Handle* handle_;
    mx_signals_t watched_signals_;
    uint64_t key_;

    utils::RefPtr<IOPortDispatcher> io_port_;

    friend struct IOPortObserverListTraits;
    utils::DoublyLinkedListNodeState<IOPortObserver*> io_port_list_node_state_;
};

struct IOPortObserverListTraits {
    inline static utils::DoublyLinkedListNodeState<IOPortObserver*>& node_state(
            IOPortObserver& obj) {
        return obj.io_port_list_node_state_;
    }
};
