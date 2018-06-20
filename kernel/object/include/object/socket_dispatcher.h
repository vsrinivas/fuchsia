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

class SocketDispatcher final : public PeeredDispatcher<SocketDispatcher> {
public:
    static zx_status_t Create(uint32_t flags, fbl::RefPtr<Dispatcher>* dispatcher0,
                              fbl::RefPtr<Dispatcher>* dispatcher1, zx_rights_t* rights);

    ~SocketDispatcher() final;

    // Dispatcher implementation.
    zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_SOCKET; }
    bool has_state_tracker() const final { return true; }

    // Socket methods.
    zx_status_t Write(user_in_ptr<const void> src, size_t len, size_t* written);

    zx_status_t WriteControl(user_in_ptr<const void> src, size_t len);

    // Shut this endpoint of the socket down for reading, writing, or both.
    zx_status_t Shutdown(uint32_t how);

    zx_status_t HalfClose();

    zx_status_t Read(user_out_ptr<void> dst, size_t len, size_t* nread);

    zx_status_t ReadControl(user_out_ptr<void> dst, size_t len, size_t* nread);

    // On success, the share queue takes ownership of |h|. On failure,
    // |h| is closed.
    zx_status_t Share(HandleOwner h);

    // On success, a HandleOwner is returned via h
    zx_status_t Accept(HandleOwner* h);

    // Property methods.
    size_t ReceiveBufferMax() const;
    size_t ReceiveBufferSize() const;
    size_t TransmitBufferMax() const;
    size_t TransmitBufferSize() const;

    zx_status_t CheckShareable(SocketDispatcher* to_send);

    struct ControlMsg {
        static constexpr size_t kSize = 1024;
        char msg[kSize];
    };

    // PeeredDispatcher implementation.
    void on_zero_handles_locked() TA_REQ(get_lock());
    void OnPeerZeroHandlesLocked() TA_REQ(get_lock());

private:
    // |control_msg| may be null.
    SocketDispatcher(fbl::RefPtr<PeerHolder<SocketDispatcher>> holder,
                     zx_signals_t starting_signals, uint32_t flags,
                     fbl::unique_ptr<ControlMsg> control_msg);
    void Init(fbl::RefPtr<SocketDispatcher> other);
    zx_status_t WriteSelfLocked(user_in_ptr<const void> src, size_t len, size_t* nwritten) TA_REQ(get_lock());
    zx_status_t WriteControlSelfLocked(user_in_ptr<const void> src, size_t len) TA_REQ(get_lock());
    zx_status_t UserSignalSelfLocked(uint32_t clear_mask, uint32_t set_mask) TA_REQ(get_lock());
    zx_status_t ShutdownOtherLocked(uint32_t how) TA_REQ(get_lock());
    zx_status_t ShareSelfLocked(HandleOwner h) TA_REQ(get_lock());

    bool is_full() const TA_REQ(get_lock()) { return data_.is_full(); }
    bool is_empty() const TA_REQ(get_lock()) { return data_.is_empty(); }

    fbl::Canary<fbl::magic("SOCK")> canary_;

    const uint32_t flags_;

    // The shared |get_lock()| protects all members below.
    MBufChain data_ TA_GUARDED(get_lock());
    fbl::unique_ptr<ControlMsg> control_msg_ TA_GUARDED(get_lock());
    size_t control_msg_len_ TA_GUARDED(get_lock());
    HandleOwner accept_queue_ TA_GUARDED(get_lock());
    bool read_disabled_ TA_GUARDED(get_lock());
};
