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

#include <magenta/state_tracker.h>

#include <mxtl/type_support.h>

mx_status_t SendIOPortPacket(IOPortDispatcher* io_port,
                             uint64_t key,
                             mx_signals_t signals) {
    mx_io_packet payload = {
        { key, MX_PORT_PKT_TYPE_IOSN, 0u},
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

IOPortClient::IOPortClient(mxtl::RefPtr<IOPortDispatcher> io_port,
                           uint64_t key, mx_signals_t signals)
    : key_(key), signals_(signals), io_port_(mxtl::move(io_port)) {
}

bool IOPortClient::Signal(mx_signals_t signals, const Mutex* mutex) {
    DEBUG_ASSERT(mutex->IsHeld());
    if ((signals & signals_) == 0)
        return true;

    if (!io_port_)
        return true;

    auto status = SendIOPortPacket(io_port_.get(), key_, signals);
    if (status == ERR_NOT_AVAILABLE) {
        // This means that the io_port has no clients but it is held
        // alive by our reference. Release the ref.
        io_port_.reset();
        return true;
    }
    return status == NO_ERROR;
}
