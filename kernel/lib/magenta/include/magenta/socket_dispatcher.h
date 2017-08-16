// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <lib/user_copy/user_ptr.h>

#include <magenta/dispatcher.h>
#include <magenta/mbuf.h>
#include <magenta/state_tracker.h>
#include <magenta/types.h>

#include <mxtl/canary.h>
#include <mxtl/intrusive_single_list.h>
#include <mxtl/mutex.h>
#include <mxtl/ref_counted.h>

class SocketDispatcher final : public Dispatcher {
public:
    static status_t Create(uint32_t flags, mxtl::RefPtr<Dispatcher>* dispatcher0,
                           mxtl::RefPtr<Dispatcher>* dispatcher1, mx_rights_t* rights);

    ~SocketDispatcher() final;

    // Dispatcher implementation.
    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_SOCKET; }
    mx_koid_t get_related_koid() const final { return peer_koid_; }
    StateTracker* get_state_tracker() final { return &state_tracker_; }
    void on_zero_handles() final;
    status_t user_signal(uint32_t clear_mask, uint32_t set_mask, bool peer) final;

    // Socket methods.
    mx_status_t Write(user_ptr<const void> src, size_t len, size_t* written);

    status_t HalfClose();

    mx_status_t Read(user_ptr<void> dst, size_t len, size_t* nread);

    void OnPeerZeroHandles();

private:
    SocketDispatcher(uint32_t flags);
    void Init(mxtl::RefPtr<SocketDispatcher> other);
    mx_status_t WriteSelf(user_ptr<const void> src, size_t len, size_t* nwritten);
    status_t UserSignalSelf(uint32_t clear_mask, uint32_t set_mask);
    status_t HalfCloseOther();

    bool is_full() const TA_REQ(lock_) { return data_.is_full(); }
    bool is_empty() const TA_REQ(lock_) { return data_.is_empty(); }

    mxtl::Canary<mxtl::magic("SOCK")> canary_;

    uint32_t flags_;
    mx_koid_t peer_koid_;
    StateTracker state_tracker_;

    // The |lock_| protects all members below.
    mxtl::Mutex lock_;
    MBufChain data_ TA_GUARDED(lock_);
    mxtl::RefPtr<SocketDispatcher> other_ TA_GUARDED(lock_);
    // half_closed_[0] is this end and [1] is the other end.
    bool half_closed_[2] TA_GUARDED(lock_);
};
