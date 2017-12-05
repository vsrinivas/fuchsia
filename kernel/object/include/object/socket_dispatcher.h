// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <lib/user_copy/user_ptr.h>
#include <object/dispatcher.h>
#include <object/handle.h>
#include <object/mbuf.h>

#include <zircon/types.h>
#include <fbl/canary.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>

constexpr size_t kControlMsgSize = 1024;

class SocketDispatcher final : public PeeredDispatcher<SocketDispatcher> {
public:
    static zx_status_t Create(uint32_t flags, fbl::RefPtr<Dispatcher>* dispatcher0,
                              fbl::RefPtr<Dispatcher>* dispatcher1, zx_rights_t* rights);

    ~SocketDispatcher() final;

    // Dispatcher implementation.
    zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_SOCKET; }
    zx_koid_t get_related_koid() const final { return peer_koid_; }
    bool has_state_tracker() const final { return true; }
    void on_zero_handles() final;
    zx_status_t user_signal(uint32_t clear_mask, uint32_t set_mask, bool peer) final;

    // Socket methods.
    zx_status_t Write(user_in_ptr<const void> src, size_t len, size_t* written);

    zx_status_t WriteControl(user_in_ptr<const void> src, size_t len);

    // Shut this endpoint of the socket down for reading, writing, or both.
    zx_status_t Shutdown(uint32_t how);

    zx_status_t HalfClose();

    zx_status_t Read(user_out_ptr<void> dst, size_t len, size_t* nread);

    zx_status_t ReadControl(user_out_ptr<void> dst, size_t len, size_t* nread);

    // On success, share takes ownership of h
    zx_status_t Share(Handle* h);

    // On success, a HandleOwner is returned via h
    zx_status_t Accept(HandleOwner* h);

    void OnPeerZeroHandles();

    zx_status_t CheckShareable(SocketDispatcher* to_send);

private:
    // The control_msg must be either nullptr or an allocation of
    // size kControlMsgSize.
    SocketDispatcher(fbl::RefPtr<PeerHolder<SocketDispatcher>> holder,
                     zx_signals_t starting_signals, uint32_t flags,
                     fbl::unique_ptr<char[]> control_msg);
    void Init(fbl::RefPtr<SocketDispatcher> other);
    zx_status_t WriteSelf(user_in_ptr<const void> src, size_t len, size_t* nwritten);
    zx_status_t WriteControlSelf(user_in_ptr<const void> src, size_t len);
    zx_status_t UserSignalSelf(uint32_t clear_mask, uint32_t set_mask);
    zx_status_t ShutdownOther(uint32_t how);
    zx_status_t ShareSelf(Handle* h);

    bool is_full() const TA_REQ(lock_) { return data_.is_full(); }
    bool is_empty() const TA_REQ(lock_) { return data_.is_empty(); }

    fbl::Canary<fbl::magic("SOCK")> canary_;

    uint32_t flags_;
    zx_koid_t peer_koid_;

    // The |lock_| protects all members below.
    fbl::Mutex lock_;
    MBufChain data_ TA_GUARDED(lock_);
    fbl::unique_ptr<char[]> control_msg_ TA_GUARDED(lock_);
    size_t control_msg_len_ TA_GUARDED(lock_);
    fbl::RefPtr<SocketDispatcher> other_ TA_GUARDED(lock_);
    HandleOwner accept_queue_ TA_GUARDED(lock_);
    bool read_disabled_ TA_GUARDED(lock_);
};
