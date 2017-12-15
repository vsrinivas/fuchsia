// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/types.h>
#include <fbl/ref_ptr.h>

#include <dispatcher-pool/dispatcher-event-source.h>

namespace dispatcher {

// class WakeupEvent
//
// WakeupEvent is one of the EventSources in the dispatcher framework used to
// implement a style of auto-reset event based on a zircon event object.
//
// :: Handler ::
//
// WakeupEvent defines a single handler (ProcessHandler) which runs when the
// event becomes signaled at least once.  Returning an error from the process
// handler will cause the event to automatically become deactivated.
//
// :: Activtation ::
//
// Activation simply requires a user to provide a valid ExecutionDomain and a
// valid ProcessHandler.  The event handle itself will allocated internally.
//
// :: Signaling ::
//
// Signaling a WakupEvent to fire is an operation protected by an internal lock
// and may be called from any thread.  Signaling a WakeupEvent multiple times
// before it gets dispatched will result in only a single dispatch event.  A
// WakeupEvent becomes un-signaled just before the registered ProcessHandler is
// called; it may become resignaled during the dispatch operation itself
// resulting in another call to the ProcessHandler (provided that the event does
// not become Deactivated)
//
class WakeupEvent : public EventSource {
public:
    static constexpr size_t MAX_HANDLER_CAPTURE_SIZE = sizeof(void*) * 2;
    using ProcessHandler =
        fbl::InlineFunction<zx_status_t(WakeupEvent*), MAX_HANDLER_CAPTURE_SIZE>;

    static fbl::RefPtr<WakeupEvent> Create();

    zx_status_t Activate(fbl::RefPtr<ExecutionDomain> domain, ProcessHandler process_handler);
    virtual void Deactivate() __TA_EXCLUDES(obj_lock_) override;
    zx_status_t Signal();

protected:
    void Dispatch(ExecutionDomain* domain) __TA_EXCLUDES(obj_lock_) override;

private:
    friend class fbl::RefPtr<WakeupEvent>;

    WakeupEvent() : EventSource(ZX_USER_SIGNAL_0) { }

    bool signaled_ __TA_GUARDED(obj_lock_) = false;
    ProcessHandler process_handler_;
};

}  // namespace dispatcher
