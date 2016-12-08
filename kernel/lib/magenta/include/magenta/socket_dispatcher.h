// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <kernel/mutex.h>

#include <magenta/dispatcher.h>
#include <magenta/state_tracker.h>
#include <magenta/types.h>

#include <mxtl/ref_counted.h>

class VmObject;
class PortClient;

class SocketDispatcher final : public Dispatcher {
public:
    static status_t Create(uint32_t flags, mxtl::RefPtr<Dispatcher>* dispatcher0,
                           mxtl::RefPtr<Dispatcher>* dispatcher1, mx_rights_t* rights);

    ~SocketDispatcher() final;

    // Dispatcher implementation.
    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_SOCKET; }
    StateTracker* get_state_tracker() final { return &state_tracker_; }
    void on_zero_handles() final;
    status_t user_signal(uint32_t clear_mask, uint32_t set_mask, bool peer) final;
    status_t set_port_client(mxtl::unique_ptr<PortClient> client) final;

    // Socket methods.
    mx_status_t Write(const void* src, size_t len, bool from_user,
                      size_t* written);

    status_t HalfClose();

    mx_status_t Read(void* dest, size_t len, bool from_user,
                     size_t* nread);

    void OnPeerZeroHandles();

private:
    class CBuf {
    public:
        ~CBuf();
        bool Init(uint32_t len);
        size_t Write(const void* src, size_t len, bool from_user);
        size_t Read(void* dest, size_t len, bool from_user);
        size_t CouldRead() const;
        size_t free() const;
        bool empty() const;

    private:
        size_t head_ = 0u;
        size_t tail_ = 0u;
        uint32_t len_pow2_ = 0u;
        char* buf_ = nullptr;
        mxtl::RefPtr<VmObject> vmo_;
    };

    SocketDispatcher(uint32_t flags);
    mx_status_t Init(mxtl::RefPtr<SocketDispatcher> other);
    mx_status_t WriteSelf(const void* src, size_t len, bool from_user,
                          size_t* nwritten);
    status_t  UserSignalSelf(uint32_t clear_mask, uint32_t set_mask);
    status_t HalfCloseOther();

    StateTracker state_tracker_;

    // The |lock_| protects all members below.
    Mutex lock_;
    CBuf cbuf_;
    mxtl::RefPtr<SocketDispatcher> other_;
    mxtl::unique_ptr<PortClient> iopc_;
    // half_closed_[0] is this end and [1] is the other end.
    bool half_closed_[2];
};
