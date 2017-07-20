// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <hypervisor/packet_mux.h>
#include <kernel/event.h>
#include <magenta/syscalls/hypervisor.h>
#include <mxalloc/new.h>

#if WITH_LIB_MAGENTA
#include <magenta/state_observer.h>

class FifoStateObserver : public StateObserver {
public:
    FifoStateObserver(mx_signals_t watched_signals)
        : watched_signals_(watched_signals) {
        event_init(&event_, false, 0);
    }

    status_t Wait(StateTracker* state_tracker) {
        state_tracker->AddObserver(this, nullptr);
        status_t status = event_wait(&event_);
        state_tracker->RemoveObserver(this);
        return status;
    }

private:
    mx_signals_t watched_signals_;
    event_t event_;

    virtual Flags OnInitialize(mx_signals_t initial_state, const CountInfo* cinfo) {
        return 0;
    }

    virtual Flags OnStateChange(mx_signals_t new_state) {
        if (new_state & watched_signals_)
            event_signal(&event_, false);
        return 0;
    }

    virtual Flags OnCancel(Handle* handle) {
        return 0;
    }
};

static status_t packet_wait(StateTracker* state_tracker, mx_signals_t signals,
                            StateReloader* reloader) {
    if (state_tracker->GetSignalsState() & signals)
        return MX_OK;
    // TODO(abdulla): Add stats to keep track of waits.
    FifoStateObserver state_observer(signals | MX_FIFO_PEER_CLOSED);
    status_t status = state_observer.Wait(state_tracker);
    reloader->Reload();
    if (status != MX_OK)
        return status;
    return state_tracker->GetSignalsState() & MX_FIFO_PEER_CLOSED ? MX_ERR_PEER_CLOSED : MX_OK;
}
#endif // WITH_LIB_MAGENTA

status_t PacketMux::AddFifo(mx_vaddr_t addr, size_t len, mxtl::RefPtr<FifoDispatcher> fifo) {
#if WITH_LIB_MAGENTA
    AllocChecker ac;
    mxtl::unique_ptr<FifoRegion> region(new (&ac) FifoRegion(addr, len, fifo));
    if (!ac.check())
        return MX_ERR_NO_MEMORY;
    AutoLock lock(&mutex);
    fifos.insert(mxtl::move(region));
    return MX_OK;
#else
    return MX_ERR_NOT_SUPPORTED;
#endif // WITH_LIB_MAGENTA
}

status_t PacketMux::FindFifo(mx_vaddr_t addr, mxtl::RefPtr<FifoDispatcher>* fifo) const {
    FifoTree::const_iterator iter;
    {
        AutoLock lock(&mutex);
        iter = fifos.upper_bound(addr);
    }
    --iter;
    if (!iter.IsValid() || !iter->InRange(addr))
        return MX_ERR_NOT_FOUND;
    *fifo = iter->fifo();
    return MX_OK;
}

status_t PacketMux::Write(mx_vaddr_t addr, const mx_guest_packet_t& packet,
                          StateReloader* reloader) const {
#if WITH_LIB_MAGENTA
    mxtl::RefPtr<FifoDispatcher> fifo;
    status_t status = FindFifo(addr, &fifo);
    if (status != MX_OK)
        return status;
    status = packet_wait(fifo->get_state_tracker(), MX_FIFO_WRITABLE, reloader);
    if (status != MX_OK)
        return status;
    const uint8_t* data = reinterpret_cast<const uint8_t*>(&packet);
    uint32_t actual;
    status = fifo->Write(data, sizeof(mx_guest_packet_t), &actual);
    if (status != MX_OK)
        return status;
    return actual != 1 ? MX_ERR_IO_DATA_INTEGRITY : MX_OK;
#else
    return MX_ERR_NOT_FOUND;
#endif // WITH_LIB_MAGENTA
}
