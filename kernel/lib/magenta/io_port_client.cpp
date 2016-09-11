// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/io_port_client.h>

#include <assert.h>
#include <err.h>
#include <new.h>
#include <platform.h>

#include <kernel/mutex.h>
#include <pow2.h>

#include <magenta/io_port_dispatcher.h>
#include <magenta/state_tracker.h>

#include <mxtl/type_support.h>

namespace {
// Converts a single signal to a value from 0 to 3. Make sure ctz does the right thing.

static_assert(MX_SIGNAL_READABLE    == 1);   // 0
static_assert(MX_SIGNAL_WRITABLE    == 2);   // 1
static_assert(MX_SIGNAL_PEER_CLOSED == 4);   // 2
static_assert(MX_SIGNAL_SIGNALED    == 8);   // 3

uint32_t signal_to_slot(mx_signals_t signal) {
    DEBUG_ASSERT(ispow2(signal));
    return __builtin_ctz(signal);
}
}

IOPortClient::IOPortClient(mxtl::RefPtr<IOPortDispatcher> io_port,
                           uint64_t key, mx_signals_t signals)
    : key_(key),
      signals_(signals),
      io_port_(mxtl::move(io_port)),
      cookie_{nullptr, nullptr, nullptr, nullptr} {
}

IOPortClient::~IOPortClient() {}

bool IOPortClient::Signal(mx_signals_t signals, const Mutex* mutex) {
    return Signal(signals, 0u, mutex);
}

bool IOPortClient::Signal(mx_signals_t signal, mx_size_t count, const Mutex* mutex) {
    DEBUG_ASSERT(signal);
    DEBUG_ASSERT(mutex->IsHeld());
    if ((signal & signals_) == 0)
        return true;

    if (!io_port_)
        return true;
    // TODO(cpu): wire back the |count|.
    auto slot = signal_to_slot(signal);
    DEBUG_ASSERT(slot < countof(cookie_));
    auto c = io_port_->Signal(cookie_[slot], key_, signal);

    if (!c) {
        io_port_.reset();
        return false;
    }

    cookie_[slot] = c;
    return true;
}
