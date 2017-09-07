// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <lib/user_copy/user_ptr.h>
#include <object/dispatcher.h>
#include <object/mbuf.h>
#include <object/state_tracker.h>

#include <magenta/types.h>
#include <fbl/canary.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>

constexpr int kControlMsgSize = 1024;

class SocketDispatcher final : public Dispatcher {
public:
    static mx_status_t Create(uint32_t flags, fbl::RefPtr<Dispatcher>* dispatcher0,
                              fbl::RefPtr<Dispatcher>* dispatcher1, mx_rights_t* rights);

    ~SocketDispatcher() final;

    // Dispatcher implementation.
    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_SOCKET; }
    mx_koid_t get_related_koid() const final { return peer_koid_; }
    StateTracker* get_state_tracker() final { return &state_tracker_; }
    void on_zero_handles() final;
    mx_status_t user_signal(uint32_t clear_mask, uint32_t set_mask, bool peer) final;

    // Socket methods.
    mx_status_t Write(user_ptr<const void> src, size_t len, size_t* written);

    mx_status_t WriteControl(user_ptr<const void> src, size_t len);

    // Shut this endpoint of the socket down for reading, writing, or both.
    mx_status_t Shutdown(uint32_t how);

    mx_status_t HalfClose();

    mx_status_t Read(user_ptr<void> dst, size_t len, size_t* nread);

    mx_status_t ReadControl(user_ptr<void> dst, size_t len, size_t* nread);

    void OnPeerZeroHandles();

private:
    explicit SocketDispatcher(uint32_t flags);
    void Init(fbl::RefPtr<SocketDispatcher> other);
    mx_status_t WriteSelf(user_ptr<const void> src, size_t len, size_t* nwritten);
    mx_status_t WriteControlSelf(user_ptr<const void> src, size_t len);
    mx_status_t UserSignalSelf(uint32_t clear_mask, uint32_t set_mask);
    mx_status_t ShutdownOther(uint32_t how);

    bool is_full() const TA_REQ(lock_) { return data_.is_full(); }
    bool is_empty() const TA_REQ(lock_) { return data_.is_empty(); }

    fbl::Canary<fbl::magic("SOCK")> canary_;

    uint32_t flags_;
    mx_koid_t peer_koid_;
    StateTracker state_tracker_;

    // The |lock_| protects all members below.
    fbl::Mutex lock_;
    MBufChain data_ TA_GUARDED(lock_);
    fbl::unique_ptr<char[]> control_msg_ TA_GUARDED(lock_);
    size_t control_msg_len_ TA_GUARDED(lock_);
    fbl::RefPtr<SocketDispatcher> other_ TA_GUARDED(lock_);
    bool read_disabled_ TA_GUARDED(lock_);
};
