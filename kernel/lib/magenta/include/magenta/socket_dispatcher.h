// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <lib/user_copy/user_ptr.h>

#include <kernel/mutex.h>

#include <magenta/dispatcher.h>
#include <magenta/state_tracker.h>
#include <magenta/types.h>

#include <mxtl/canary.h>
#include <mxtl/intrusive_single_list.h>
#include <mxtl/ref_counted.h>

class VmMapping;
class VmObject;
class PortClient;

constexpr int kMBufSize = 2048 - 8 - 4 - 4;

constexpr int kSocketSizeMax = 128 * kMBufSize;

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
    status_t set_port_client(mxtl::unique_ptr<PortClient> client) final;

    // Socket methods.
    mx_status_t Write(user_ptr<const void> src, size_t len, size_t* written);

    status_t HalfClose();

    mx_status_t Read(user_ptr<void> dst, size_t len, size_t* nread);

    void OnPeerZeroHandles();

private:
    // An MBuf is a small fixed-size chainable memory buffer.
    struct MBuf : public mxtl::SinglyLinkedListable<MBuf*> {
        size_t rem() const;

        uint32_t off_ = 0u;
        uint32_t len_ = 0u;
        char data_[kMBufSize] = {0};
        // TODO: maybe union data_ with char* blocks for large messages
    };
    static_assert(sizeof(MBuf) == 2048, "");

    SocketDispatcher(uint32_t flags);
    mx_status_t Init(mxtl::RefPtr<SocketDispatcher> other);
    mx_status_t WriteSelf(user_ptr<const void> src, size_t len, size_t* nwritten);
    status_t UserSignalSelf(uint32_t clear_mask, uint32_t set_mask);
    status_t HalfCloseOther();

    size_t WriteMBufs(user_ptr<const void> src, size_t len) TA_REQ(lock_);
    size_t ReadMBufs(user_ptr<void> dst, size_t len) TA_REQ(lock_);
    MBuf* AllocMBuf() TA_REQ(lock_);
    void FreeMBuf(MBuf* buf) TA_REQ(lock_);
    bool is_full() const TA_REQ(lock_);
    bool is_empty() const TA_REQ(lock_);

    mxtl::Canary<mxtl::magic("SOCK")> canary_;

    mx_koid_t peer_koid_;
    StateTracker state_tracker_;

    // The |lock_| protects all members below.
    Mutex lock_;
    mxtl::SinglyLinkedList<MBuf*> freelist_ TA_GUARDED(lock_);
    mxtl::SinglyLinkedList<MBuf*> tail_ TA_GUARDED(lock_);
    MBuf* head_ TA_GUARDED(lock_);
    size_t size_ TA_GUARDED(lock_);
    mxtl::RefPtr<SocketDispatcher> other_ TA_GUARDED(lock_);
    mxtl::unique_ptr<PortClient> iopc_ TA_GUARDED(lock_);
    // half_closed_[0] is this end and [1] is the other end.
    bool half_closed_[2] TA_GUARDED(lock_);
};
