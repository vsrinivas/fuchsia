// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/rights.h>
#include <zircon/types.h>

#include <fbl/canary.h>
#include <fbl/mutex.h>
#include <fbl/ref_ptr.h>
#include <object/dispatcher.h>
#include <object/handle.h>
#include <sys/types.h>

class EventPairDispatcher final :
    public PeeredDispatcher<EventPairDispatcher, ZX_DEFAULT_EVENTPAIR_RIGHTS, ZX_EVENT_SIGNALED> {
public:
    static zx_status_t Create(KernelHandle<EventPairDispatcher>* handle0,
                              KernelHandle<EventPairDispatcher>* handle1, zx_rights_t* rights);

    ~EventPairDispatcher() final;
    zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_EVENTPAIR; }
    CookieJar* get_cookie_jar() final { return &cookie_jar_; }

    // PeeredDispatcher implementation.
    void on_zero_handles_locked() TA_REQ(get_lock());
    void OnPeerZeroHandlesLocked() TA_REQ(get_lock());

private:
    explicit EventPairDispatcher(fbl::RefPtr<PeerHolder<EventPairDispatcher>> holder);
    void Init(fbl::RefPtr<EventPairDispatcher> other);

    CookieJar cookie_jar_;
};
