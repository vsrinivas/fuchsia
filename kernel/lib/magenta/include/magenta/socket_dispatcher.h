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

constexpr int kMBufHeaderSize = 8 + (4 * 4);

constexpr int kMBufSize = 2048 - 16;

constexpr int kMBufDataSize = kMBufSize - kMBufHeaderSize;

constexpr int kSocketSizeMax = 128 * kMBufDataSize;

// An MBuf is a small fixed-size chainable memory buffer.
class MBuf : public mxtl::SinglyLinkedListable<MBuf*> {
public:
    size_t rem() const;

    uint32_t off_ = 0u;
    uint32_t len_ = 0u;
    // pkt_len_ is set to the total number of bytes in a packet
    // when a socket is in MX_SOCKET_DATAGRAM mode. A pkt_len_ of
    // 0 means this mbuf is part of the body of a packet.
    //
    // Always 0 in MX_SOCKET_STREAM mode.
    uint32_t pkt_len_ = 0u;
    uint32_t unused;
    char data_[kMBufDataSize] = {0};
    // TODO: maybe union data_ with char* blocks for large messages
};
static_assert(sizeof(MBuf) == kMBufSize, "");

class MBufChain {
public:
    MBufChain() = default;
    ~MBufChain();

    mx_status_t WriteStreamMBufs(user_ptr<const void> src, size_t len, size_t* written);
    mx_status_t WriteDgramMBufs(user_ptr<const void> src, size_t len, size_t* written);
    size_t ReadMBufs(user_ptr<void> dst, size_t len, bool datagram);
    MBuf* AllocMBuf();
    void FreeMBuf(MBuf* buf);
    bool is_full() const;
    bool is_empty() const;
    size_t size() const { return size_; }

private:
    mxtl::SinglyLinkedList<MBuf*> freelist_;
    mxtl::SinglyLinkedList<MBuf*> tail_;
    MBuf* head_ = nullptr;;
    size_t size_ = 0u;
};

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

    // Shut this endpoint of the socket down for reading, writing, or both.
    status_t Shutdown(uint32_t how);

    mx_status_t Read(user_ptr<void> dst, size_t len, size_t* nread);

    void OnPeerZeroHandles();

private:
    SocketDispatcher(uint32_t flags);
    void Init(mxtl::RefPtr<SocketDispatcher> other);
    mx_status_t WriteSelf(user_ptr<const void> src, size_t len, size_t* nwritten);
    status_t UserSignalSelf(uint32_t clear_mask, uint32_t set_mask);
    status_t ShutdownOther(uint32_t how);

    bool is_full() const TA_REQ(lock_) { return data_.is_full(); }
    bool is_empty() const TA_REQ(lock_) { return data_.is_empty(); }

    mxtl::Canary<mxtl::magic("SOCK")> canary_;

    uint32_t flags_;
    mx_koid_t peer_koid_;
    StateTracker state_tracker_;

    // The |lock_| protects all members below.
    Mutex lock_;
    MBufChain data_ TA_GUARDED(lock_);
    mxtl::RefPtr<SocketDispatcher> other_ TA_GUARDED(lock_);
};
