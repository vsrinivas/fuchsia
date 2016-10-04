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
class IOPortClient;

class SocketDispatcher final : public Dispatcher {
public:
    static status_t Create(uint32_t flags, mxtl::RefPtr<Dispatcher>* dispatcher0,
                           mxtl::RefPtr<Dispatcher>* dispatcher1, mx_rights_t* rights);

    ~SocketDispatcher() final;

    // Dispatcher implementation.
    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_SOCKET; }
    StateTracker* get_state_tracker() final { return &state_tracker_; }
    void on_zero_handles() final;
    status_t set_port_client(mxtl::unique_ptr<IOPortClient> client) final;

    // Socket methods.
    mx_ssize_t Write(const void* src, mx_size_t len, bool from_user) {
        return WriteHelper(src, len, from_user, false);
    }
    mx_ssize_t OOB_Write(const void* src, mx_size_t len, bool from_user) {
        return WriteHelper(src, len, from_user, true);
    }

    status_t HalfClose();

    mx_ssize_t Read(void* dest, mx_size_t len, bool from_user);
    mx_ssize_t OOB_Read(void* dest, mx_size_t len, bool from_user);

    void OnPeerZeroHandles();

    status_t UserSignal(uint32_t clear_mask, uint32_t set_mask) final;

private:
    class CBuf {
    public:
        ~CBuf();
        bool Init(uint32_t len);
        mx_size_t Write(const void* src, mx_size_t len, bool from_user);
        mx_size_t Read(void* dest, mx_size_t len, bool from_user);
        mx_size_t free() const;
        bool empty() const;

    private:
        mx_size_t head_ = 0u;
        mx_size_t tail_ = 0u;
        uint32_t len_pow2_ = 0u;
        char* buf_ = nullptr;
        mxtl::RefPtr<VmObject> vmo_;
    };

    SocketDispatcher(uint32_t flags);
    mx_status_t Init(mxtl::RefPtr<SocketDispatcher> other);
    mx_ssize_t WriteHelper(const void* src, mx_size_t len, bool from_user, bool is_oob);
    mx_ssize_t WriteSelf(const void* src, mx_size_t len, bool from_user);
    mx_ssize_t OOB_WriteSelf(const void* src, mx_size_t len, bool from_user);
    status_t  UserSignalSelf(uint32_t clear_mask, uint32_t set_mask);
    status_t HalfCloseOther();

    const uint32_t flags_;
    NonIrqStateTracker state_tracker_;

    // The |lock_| protects all members below.
    Mutex lock_;
    CBuf cbuf_;
    mxtl::RefPtr<SocketDispatcher> other_;
    mxtl::unique_ptr<IOPortClient> iopc_;
    mx_size_t oob_len_;
    // half_closed_[0] is this end and [1] is the other end.
    bool half_closed_[2];
    char oob_[MX_SOCKET_CONTROL_MAX_LEN];
};
