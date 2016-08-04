// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <platform.h>

#include <arch/ops.h>

#include <magenta/io_port_observer.h>
#include <magenta/state_tracker.h>

#include <utils/type_support.h>

bool SendIOPortPacket(IOPortDispatcher* io_port,
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
      return false;

    return io_port->Queue(packet) == NO_ERROR;
}

IOPortObserver::IOPortObserver(utils::RefPtr<IOPortDispatcher> io_port,
                               Handle* handle,
                               mx_signals_t watched_signals,
                               uint64_t key)
    : state_(NEW),
      handle_(handle),
      watched_signals_(watched_signals),
      key_(key),
      io_port_(utils::move(io_port)) {
}

int IOPortObserver::GetState() {
    return atomic_load(&state_);
}

int IOPortObserver::SetState(int state) {
    return atomic_swap(&state_, state);
}

bool IOPortObserver::OnInitialize(mx_signals_state_t) {
    return false;
}

bool IOPortObserver::OnStateChange(mx_signals_state_t new_state) {
    return MaybeSignal(new_state);
}

bool IOPortObserver::OnCancel(Handle* handle, bool* should_remove, bool* call_did_cancel) {
    if (handle != handle_)
        return false;

    handle_ = nullptr;
    // atomically check that IOPortDispatcher::Unbind() is not in progress and take
    // this path if we arrive first.
    int expected = NEW;
    if (!atomic_cmpxchg(&state_, &expected, CANCELLED))
      return false;

    *should_remove = true;
    *call_did_cancel = true;
    return false;
}

void IOPortObserver::OnDidCancel() {
    // Called outside any lock.
    io_port_->CancelObserver(this);
}

bool IOPortObserver::MaybeSignal(mx_signals_state_t state) {
    auto match = state.satisfied & watched_signals_;
    return match ? SendIOPortPacket(io_port_.get(), key_, match) : false;
}
