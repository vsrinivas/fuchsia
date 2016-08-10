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

#include <utils/ref_counted.h>

class SocketDispatcher final : public Dispatcher {
public:
    static status_t Create(uint32_t flags, utils::RefPtr<Dispatcher>* dispatcher0,
                           utils::RefPtr<Dispatcher>* dispatcher1, mx_rights_t* rights);

    ~SocketDispatcher() final;

    // Dispatcher implementation.
    mx_obj_type_t GetType() const final { return MX_OBJ_TYPE_SOCKET; }
    SocketDispatcher* get_socket_dispatcher() final { return this; }
    StateTracker* get_state_tracker() final { return &state_tracker_; }
    void on_zero_handles() final;

    // Socket methods.
    mx_ssize_t Write(const void* src, mx_size_t len, bool from_user);
    mx_ssize_t Read(void* dest, mx_size_t len, bool from_user);
    void OnPeerZeroHandles();

private:
    class CBuf {
    public:
        ~CBuf();
        bool Init(uint32_t len);
        mx_size_t Write(const void* src, mx_size_t len, bool from_user);
        mx_size_t Read(void* dest, mx_size_t len, bool from_user);
        mx_size_t available() const;

    private:
        mx_size_t head_ = 0u;
        mx_size_t tail_ = 0u;
        uint32_t len_pow2_ = 0u;
        char* buf_ = nullptr;
    };

    SocketDispatcher(uint32_t flags);
    mx_status_t Init(utils::RefPtr<SocketDispatcher> other);
    mx_ssize_t WriteSelf(const void* src, mx_size_t len, bool from_user);

    const uint32_t flags_;
    StateTracker state_tracker_;
    utils::RefPtr<SocketDispatcher> other_;

    // The |lock_| protects cbuf_.
    mutex_t lock_;
    CBuf cbuf_;
};
