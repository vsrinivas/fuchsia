// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/port_client.h>

#include <assert.h>
#include <err.h>
#include <platform.h>

#include <kernel/mutex.h>
#include <pow2.h>

#include <magenta/port_dispatcher.h>
#include <magenta/state_tracker.h>

#include <mxtl/type_support.h>

namespace {

uint32_t signal_to_slot(mx_signals_t signal) {
    DEBUG_ASSERT(ispow2(signal));
    return __builtin_ctz(signal);
}
}

PortClient::PortClient(mxtl::RefPtr<PortDispatcher> port,
                       uint64_t key, mx_signals_t signals)
    : key_(key),
      signals_(signals),
      port_(mxtl::move(port)),
      cookie_{} {
}

PortClient::~PortClient() {}

bool PortClient::Signal(mx_signals_t signals, const Mutex* mutex) {
    return Signal(signals, 0u, mutex);
}

bool PortClient::Signal(mx_signals_t signal, size_t count, const Mutex* mutex) {
    canary_.Assert();

    DEBUG_ASSERT(signal);
    DEBUG_ASSERT(mutex->IsHeld());
    if ((signal & signals_) == 0)
        return true;

    if (!port_)
        return true;
    // TODO(cpu): wire back the |count|.
    auto slot = signal_to_slot(signal);
    DEBUG_ASSERT(slot < countof(cookie_));
    auto c = port_->Signal(cookie_[slot], key_, signal);

    if (c == nullptr) {
        port_.reset();
        return false;
    }

    DEBUG_ASSERT((cookie_[slot] == nullptr) || (c == cookie_[slot]));
    cookie_[slot] = c;
    return true;
}
