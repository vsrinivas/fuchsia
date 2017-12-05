// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/types.h>
#include <fbl/canary.h>
#include <fbl/mutex.h>
#include <fbl/ref_ptr.h>
#include <object/dispatcher.h>
#include <sys/types.h>

class EventPairDispatcher final : public PeeredDispatcher<EventPairDispatcher> {
public:
    static zx_status_t Create(fbl::RefPtr<Dispatcher>* dispatcher0,
                              fbl::RefPtr<Dispatcher>* dispatcher1,
                              zx_rights_t* rights);

    ~EventPairDispatcher() final;
    zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_EVENT_PAIR; }
    bool has_state_tracker() const final { return true; }
    CookieJar* get_cookie_jar() final { return &cookie_jar_; }
    zx_status_t user_signal(uint32_t clear_mask, uint32_t set_mask, bool peer) final;
    void on_zero_handles() final;
    zx_koid_t get_related_koid() const final { return other_koid_; }

private:
    explicit EventPairDispatcher(fbl::RefPtr<PeerHolder<EventPairDispatcher>> holder);
    void Init(fbl::RefPtr<EventPairDispatcher> other);

    CookieJar cookie_jar_;

    fbl::Canary<fbl::magic("EVPD")> canary_;

    // Set in Init(); never changes otherwise.
    zx_koid_t other_koid_;

    // Protects |other_| (except in Init(), where it's not needed).
    fbl::Mutex lock_;
    fbl::RefPtr<EventPairDispatcher> other_ TA_GUARDED(lock_);
};
