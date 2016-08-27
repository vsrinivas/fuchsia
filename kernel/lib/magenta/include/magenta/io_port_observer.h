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
    static IOPortObserver* Create(mxtl::RefPtr<IOPortDispatcher> io_port,
                                  Handle* handle,
                                  mx_signals_t watched_signals,
                                  uint64_t key);

    // Calling this method causes the object destruction. Use with care.
    void OnDidCancel() final;

private:
    IOPortObserver() = delete;
    IOPortObserver(const IOPortObserver&) = delete;
    IOPortObserver& operator=(const IOPortObserver&) = delete;

    IOPortObserver(mxtl::RefPtr<IOPortDispatcher> io_port,
                   Handle* handle,
                   mx_signals_t watched_signals,
                   uint64_t key);

    ~IOPortObserver() = default;

    // StateObserver implementation:
    bool OnInitialize(mx_signals_state_t initial_state) final;
    bool OnStateChange(mx_signals_state_t new_state) final;
    bool OnCancel(Handle* handle, bool* should_remove, bool* call_did_cancel) final;

    Handle* handle_;
    mx_signals_t watched_signals_;
    uint64_t key_;

    mxtl::RefPtr<IOPortDispatcher> io_port_;
};

