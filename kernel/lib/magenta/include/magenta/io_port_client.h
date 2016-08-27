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
class Mutex;

class IOPortClient {
public:
    IOPortClient(mxtl::RefPtr<IOPortDispatcher> io_port, uint64_t key, mx_signals_t signals);

    IOPortClient() = default;
    bool Signal(mx_signals_t signals, const Mutex* mutex);

private:
    IOPortClient(const IOPortClient&) = delete;
    IOPortClient& operator=(const IOPortClient&) = delete;

    const uint64_t key_;
    const mx_signals_t signals_;
    mxtl::RefPtr<IOPortDispatcher> io_port_;
};
