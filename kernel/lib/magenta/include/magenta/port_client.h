// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <magenta/types.h>

#include <mxtl/ref_ptr.h>

class PortDispatcher;
class Mutex;

class PortClient {
public:
    PortClient(mxtl::RefPtr<PortDispatcher> port, uint64_t key, mx_signals_t signals);
    ~PortClient();

    mx_signals_t get_trigger_signals() const { return signals_; }
    bool Signal(mx_signals_t signals, const Mutex* mutex);
    bool Signal(mx_signals_t signals, mx_size_t byte_count, const Mutex* mutex);

private:
    PortClient() = delete;
    PortClient(const PortClient&) = delete;
    PortClient& operator=(const PortClient&) = delete;

    const uint64_t key_;
    const mx_signals_t signals_;
    mxtl::RefPtr<PortDispatcher> port_;
    void* cookie_[8];
};
