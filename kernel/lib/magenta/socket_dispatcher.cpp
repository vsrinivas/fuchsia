// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/socket_dispatcher.h>

#include <string.h>

#include <assert.h>
#include <err.h>
#include <pow2.h>
#include <trace.h>

#include <lib/user_copy/user_ptr.h>

#include <kernel/auto_lock.h>
#include <kernel/vm/vm_aspace.h>
#include <kernel/vm/vm_object.h>
#include <kernel/vm/vm_object_paged.h>

#include <magenta/handle.h>
#include <magenta/port_client.h>
#include <mxalloc/new.h>

#define LOCAL_TRACE 0

constexpr mx_rights_t kDefaultSocketRights =
    MX_RIGHT_TRANSFER | MX_RIGHT_DUPLICATE | MX_RIGHT_READ | MX_RIGHT_WRITE;

constexpr mx_signals_t kValidSignalMask =
    MX_SOCKET_READABLE | MX_SOCKET_PEER_CLOSED | MX_USER_SIGNAL_ALL;

size_t SocketDispatcher::MBuf::rem() const {
    return kMBufSize - (off_ + len_);
}

bool SocketDispatcher::is_full() const {
    return size_ >= kSocketSizeMax;
}

bool SocketDispatcher::is_empty() const {
    return size_ == 0;
}

// static
status_t SocketDispatcher::Create(uint32_t flags,
                                  mxtl::RefPtr<Dispatcher>* dispatcher0,
                                  mxtl::RefPtr<Dispatcher>* dispatcher1,
                                  mx_rights_t* rights) {
    LTRACE_ENTRY;

    AllocChecker ac;
    auto socket0 = mxtl::AdoptRef(new (&ac) SocketDispatcher(flags));
    if (!ac.check())
        return ERR_NO_MEMORY;

    auto socket1 = mxtl::AdoptRef(new (&ac) SocketDispatcher(flags));
    if (!ac.check())
        return ERR_NO_MEMORY;

    mx_status_t status;
    if ((status = socket0->Init(socket1)) != NO_ERROR)
        return status;
    if ((status = socket1->Init(socket0)) != NO_ERROR)
        return status;

    *rights = kDefaultSocketRights;
    *dispatcher0 = mxtl::RefPtr<Dispatcher>(socket0.get());
    *dispatcher1 = mxtl::RefPtr<Dispatcher>(socket1.get());
    return NO_ERROR;
}

SocketDispatcher::SocketDispatcher(uint32_t /*flags*/)
    : peer_koid_(0u),
      state_tracker_(MX_SOCKET_WRITABLE),
      head_(nullptr),
      size_(0u),
      half_closed_{false, false} {
}

SocketDispatcher::~SocketDispatcher() {
    while (!tail_.is_empty())
        delete tail_.pop_front();
    while (!freelist_.is_empty())
        delete freelist_.pop_front();
}

// This is called before either SocketDispatcher is accessible from threads other than the one
// initializing the socket, so it does not need locking.
mx_status_t SocketDispatcher::Init(mxtl::RefPtr<SocketDispatcher> other) TA_NO_THREAD_SAFETY_ANALYSIS {
    other_ = mxtl::move(other);
    peer_koid_ = other_->get_koid();
    return NO_ERROR;
}

void SocketDispatcher::on_zero_handles() {
    canary_.Assert();

    mxtl::RefPtr<SocketDispatcher> socket;
    {
        AutoLock lock(&lock_);
        socket = mxtl::move(other_);
    }
    if (!socket)
        return;

    socket->OnPeerZeroHandles();
}

void SocketDispatcher::OnPeerZeroHandles() {
    canary_.Assert();

    AutoLock lock(&lock_);
    other_.reset();
    state_tracker_.UpdateState(MX_SOCKET_WRITABLE, MX_SOCKET_PEER_CLOSED);
    if (iopc_)
        iopc_->Signal(MX_SOCKET_PEER_CLOSED, &lock_);
}

status_t SocketDispatcher::user_signal(uint32_t clear_mask, uint32_t set_mask, bool peer) {
    canary_.Assert();

    if ((set_mask & ~MX_USER_SIGNAL_ALL) || (clear_mask & ~MX_USER_SIGNAL_ALL))
        return ERR_INVALID_ARGS;

    if (!peer) {
        state_tracker_.UpdateState(clear_mask, set_mask);
        return NO_ERROR;
    }

    mxtl::RefPtr<SocketDispatcher> other;
    {
        AutoLock lock(&lock_);
        if (!other_)
            return ERR_PEER_CLOSED;
        other = other_;
    }

    return other->UserSignalSelf(clear_mask, set_mask);
}

status_t SocketDispatcher::UserSignalSelf(uint32_t clear_mask, uint32_t set_mask) {
    canary_.Assert();

    AutoLock lock(&lock_);
    auto satisfied = state_tracker_.GetSignalsState();
    auto changed = ~satisfied & set_mask;

    if (changed) {
        if (iopc_)
            iopc_->Signal(changed, 0u, &lock_);
    }

    state_tracker_.UpdateState(clear_mask, set_mask);
    return NO_ERROR;
}

status_t SocketDispatcher::set_port_client(mxtl::unique_ptr<PortClient> client) {
    canary_.Assert();

    if ((client->get_trigger_signals() & ~kValidSignalMask) != 0)
        return ERR_INVALID_ARGS;

    AutoLock lock(&lock_);
    if (iopc_)
        return ERR_BAD_STATE;

    iopc_ = mxtl::move(client);

    if (!is_empty())
        iopc_->Signal(MX_SOCKET_READABLE, 0u, &lock_);

    return NO_ERROR;
}

status_t SocketDispatcher::HalfClose() {
    canary_.Assert();

    mxtl::RefPtr<SocketDispatcher> other;
    {
        AutoLock lock(&lock_);
        if (half_closed_[0])
            return NO_ERROR;
        if (!other_)
            return ERR_PEER_CLOSED;
        other = other_;
        half_closed_[0] = true;
        state_tracker_.UpdateState(MX_SOCKET_WRITABLE, 0u);
    }
    return other->HalfCloseOther();
}

status_t SocketDispatcher::HalfCloseOther() {
    canary_.Assert();

    AutoLock lock(&lock_);
    half_closed_[1] = true;
    state_tracker_.UpdateState(0u, MX_SOCKET_PEER_CLOSED);
    return NO_ERROR;
}

mx_status_t SocketDispatcher::Write(user_ptr<const void> src, size_t len,
                                    size_t* nwritten) {
    canary_.Assert();

    mxtl::RefPtr<SocketDispatcher> other;
    {
        AutoLock lock(&lock_);
        if (!other_)
            return ERR_PEER_CLOSED;
        if (half_closed_[0])
            return ERR_BAD_STATE;
        other = other_;
    }

    return other->WriteSelf(src, len, nwritten);
}

mx_status_t SocketDispatcher::WriteSelf(user_ptr<const void> src, size_t len,
                                        size_t* written) {
    canary_.Assert();

    AutoLock lock(&lock_);

    if (is_full())
        return ERR_SHOULD_WAIT;

    bool was_empty = is_empty();

    auto st = WriteMBufs(src, len);

    if (st > 0) {
        if (was_empty)
            state_tracker_.UpdateState(0u, MX_SOCKET_READABLE);
        if (iopc_)
            iopc_->Signal(MX_SOCKET_READABLE, st, &lock_);
    }

    if (is_full())
        other_->state_tracker_.UpdateState(MX_SOCKET_WRITABLE, 0u);

    *written = st;
    return NO_ERROR;
}

size_t SocketDispatcher::WriteMBufs(user_ptr<const void> src, size_t len) {
    if (head_ == nullptr) {
        head_ = AllocMBuf();
        if (head_ == nullptr)
            return 0;
        tail_.push_front(head_);
    }

    size_t pos = 0;
    while (pos < len) {
        if (head_->rem() == 0) {
            auto next = AllocMBuf();
            if (next == nullptr)
                return pos;
            tail_.insert_after(tail_.make_iterator(*head_), next);
            head_ = next;
        }
        void* dst = head_->data_ + head_->off_ + head_->len_;
        size_t copy_len = MIN(head_->rem(), len - pos);
        if (size_ + copy_len > kSocketSizeMax) {
            copy_len = kSocketSizeMax - size_;
            if (copy_len == 0)
                break;
        }
        if (src.byte_offset(pos).copy_array_from_user(dst, copy_len) != NO_ERROR)
            return pos;
        pos += copy_len;
        head_->len_ += static_cast<uint32_t>(copy_len);
        size_ += copy_len;
    }
    return pos;
}

mx_status_t SocketDispatcher::Read(user_ptr<void> dst, size_t len,
                                   size_t* nread) {
    canary_.Assert();

    AutoLock lock(&lock_);

    // Just query for bytes outstanding.
    if (!dst && len == 0) {
        *nread = size_;
        return NO_ERROR;
    }

    bool closed = half_closed_[1] || !other_;

    if (is_empty())
        return closed ? ERR_PEER_CLOSED : ERR_SHOULD_WAIT;

    bool was_full = is_full();

    auto st = ReadMBufs(dst, len);

    if (is_empty())
        state_tracker_.UpdateState(MX_SOCKET_READABLE, 0u);

    if (!closed && was_full && (st > 0))
        other_->state_tracker_.UpdateState(0u, MX_SOCKET_WRITABLE);

    *nread = static_cast<size_t>(st);
    return NO_ERROR;
}

size_t SocketDispatcher::ReadMBufs(user_ptr<void> dst, size_t len) {
    size_t pos = 0;
    while (pos < len) {
        if (tail_.is_empty())
            return pos;
        MBuf& cur = tail_.front();
        char* src = cur.data_ + cur.off_;
        size_t copy_len = MIN(cur.len_, len - pos);
        if (dst.byte_offset(pos).copy_array_to_user(src, copy_len) != NO_ERROR)
            return pos;
        pos += copy_len;
        cur.off_ += static_cast<uint32_t>(copy_len);
        cur.len_ -= static_cast<uint32_t>(copy_len);
        size_ -= copy_len;
        if (cur.len_ == 0) {
            if (head_ == &cur)
                head_ = nullptr;
            FreeMBuf(tail_.pop_front());
        }
    }
    return pos;
}

SocketDispatcher::MBuf* SocketDispatcher::AllocMBuf() {
    if (freelist_.is_empty()) {
        AllocChecker ac;
        MBuf* buf = new (&ac) MBuf();
        return (!ac.check()) ? nullptr : buf;
    }
    return freelist_.pop_front();
}

void SocketDispatcher::FreeMBuf(SocketDispatcher::MBuf* buf) {
    buf->off_ = 0u;
    buf->len_ = 0u;
    freelist_.push_front(buf);
}
