// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <err.h>
#include <new.h>
#include <platform.h>

#include <magenta/io_port_observer.h>
#include <magenta/state_tracker.h>

#include <utils/type_support.h>

mx_status_t SendIOPortPacket(IOPortDispatcher* io_port,
                             uint64_t key,
                             mx_signals_t signals) {
    mx_io_packet payload = {
        { key, MX_IO_PORT_PKT_TYPE_IOSN, 0u},
        current_time_hires(),
        0u,                   //  TODO(cpu): support bytes (for pipes)
        signals,
        0u
    };

    auto packet = IOP_Packet::Make(&payload, sizeof(payload));
    if (!packet)
      return ERR_NO_MEMORY;

    return io_port->Queue(packet);
}

IOPortObserver* IOPortObserver::Create(utils::RefPtr<IOPortDispatcher> io_port,
                                       Handle* handle,
                                       mx_signals_t signals,
                                       uint64_t key) {
    AllocChecker ac;
    auto observer =
        new (&ac) IOPortObserver(utils::move(io_port), handle, signals, key);
    return ac.check() ? observer : nullptr;
}

IOPortObserver::IOPortObserver(utils::RefPtr<IOPortDispatcher> io_port,
                               Handle* handle,
                               mx_signals_t watched_signals,
                               uint64_t key)
    : handle_(handle),
      watched_signals_(watched_signals),
      key_(key),
      io_port_(utils::move(io_port)) {
}

bool IOPortObserver::OnInitialize(mx_signals_state_t) {
    return false;
}

bool IOPortObserver::OnStateChange(mx_signals_state_t new_state) {
    if (!io_port_)
      return false;

    auto match = new_state.satisfied & watched_signals_;
    if (!match)
      return false;

    if (SendIOPortPacket(io_port_.get(), key_, match) != NO_ERROR) {
      io_port_.reset();
      return false;
    }
    return true;
}

bool IOPortObserver::OnCancel(Handle* handle, bool* should_remove, bool* call_did_cancel) {
    if (handle != handle_)
        return false;

    *should_remove = true;
    *call_did_cancel = true;
    return false;
}

void IOPortObserver::OnDidCancel() {
    // Called outside any lock.
    delete this;
}

