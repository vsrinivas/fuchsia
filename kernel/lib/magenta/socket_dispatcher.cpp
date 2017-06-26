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
#include <magenta/rights.h>
#include <mxalloc/new.h>

#define LOCAL_TRACE 0

size_t SocketDispatcher::MBuf::rem() const {
    return kMBufDataSize - (off_ + len_);
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
        return MX_ERR_NO_MEMORY;

    auto socket1 = mxtl::AdoptRef(new (&ac) SocketDispatcher(flags));
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    mx_status_t status;
    if ((status = socket0->Init(socket1)) != MX_OK)
        return status;
    if ((status = socket1->Init(socket0)) != MX_OK)
        return status;

    *rights = MX_DEFAULT_SOCKET_RIGHTS;
    *dispatcher0 = mxtl::RefPtr<Dispatcher>(socket0.get());
    *dispatcher1 = mxtl::RefPtr<Dispatcher>(socket1.get());
    return MX_OK;
}

SocketDispatcher::SocketDispatcher(uint32_t flags)
    : flags_(flags),
      peer_koid_(0u),
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
    if (flags_ != MX_SOCKET_STREAM && flags_ != MX_SOCKET_DATAGRAM) {
        return MX_ERR_INVALID_ARGS;
    }
    return MX_OK;
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
}

status_t SocketDispatcher::user_signal(uint32_t clear_mask, uint32_t set_mask, bool peer) {
    canary_.Assert();

    if ((set_mask & ~MX_USER_SIGNAL_ALL) || (clear_mask & ~MX_USER_SIGNAL_ALL))
        return MX_ERR_INVALID_ARGS;

    if (!peer) {
        state_tracker_.UpdateState(clear_mask, set_mask);
        return MX_OK;
    }

    mxtl::RefPtr<SocketDispatcher> other;
    {
        AutoLock lock(&lock_);
        if (!other_)
            return MX_ERR_PEER_CLOSED;
        other = other_;
    }

    return other->UserSignalSelf(clear_mask, set_mask);
}

status_t SocketDispatcher::UserSignalSelf(uint32_t clear_mask, uint32_t set_mask) {
    canary_.Assert();
    state_tracker_.UpdateState(clear_mask, set_mask);
    return MX_OK;
}

status_t SocketDispatcher::HalfClose() {
    canary_.Assert();

    mxtl::RefPtr<SocketDispatcher> other;
    {
        AutoLock lock(&lock_);
        if (half_closed_[0])
            return MX_OK;
        if (!other_)
            return MX_ERR_PEER_CLOSED;
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
    return MX_OK;
}

mx_status_t SocketDispatcher::Write(user_ptr<const void> src, size_t len,
                                    size_t* nwritten) {
    canary_.Assert();

    mxtl::RefPtr<SocketDispatcher> other;
    {
        AutoLock lock(&lock_);
        if (!other_)
            return MX_ERR_PEER_CLOSED;
        if (half_closed_[0])
            return MX_ERR_BAD_STATE;
        other = other_;
    }

    if (len == 0) {
        *nwritten = 0;
        return MX_OK;
    }
    if (len != static_cast<size_t>(static_cast<uint32_t>(len)))
        return MX_ERR_INVALID_ARGS;

    return other->WriteSelf(src, len, nwritten);
}

mx_status_t SocketDispatcher::WriteSelf(user_ptr<const void> src, size_t len,
                                        size_t* written) {
    canary_.Assert();

    AutoLock lock(&lock_);

    if (is_full())
        return MX_ERR_SHOULD_WAIT;

    bool was_empty = is_empty();

    size_t st = 0u;
    mx_status_t status;
    if (flags_ == MX_SOCKET_DATAGRAM) {
        status = WriteDgramMBufsLocked(src, len, &st);
    } else {
        status = WriteStreamMBufsLocked(src, len, &st);
    }
    if (status)
        return status;

    if (st > 0) {
        if (was_empty)
            state_tracker_.UpdateState(0u, MX_SOCKET_READABLE);
    }

    if (is_full())
        other_->state_tracker_.UpdateState(MX_SOCKET_WRITABLE, 0u);

    *written = st;
    return status;
}

mx_status_t SocketDispatcher::WriteDgramMBufsLocked(user_ptr<const void> src,
                                                    size_t len, size_t* written) {
    if (len + size_ > kSocketSizeMax)
        return MX_ERR_SHOULD_WAIT;

    mxtl::SinglyLinkedList<MBuf*> bufs;
    for (size_t need = 1 + ((len - 1) / kMBufDataSize); need != 0; need--) {
        auto buf = AllocMBuf();
        if (buf == nullptr) {
            while (!bufs.is_empty())
                FreeMBuf(bufs.pop_front());
            return MX_ERR_SHOULD_WAIT;
        }
        bufs.push_front(buf);
    }

    size_t pos = 0;
    for (auto& buf : bufs) {
        size_t copy_len = MIN(kMBufDataSize, len - pos);
        if (src.byte_offset(pos).copy_array_from_user(buf.data_, copy_len) != MX_OK) {
            while (!bufs.is_empty())
                FreeMBuf(bufs.pop_front());
            return MX_ERR_INVALID_ARGS; // Bad user buffer.
        }
        pos += copy_len;
        buf.len_ += static_cast<uint32_t>(copy_len);
    }

    bufs.front().pkt_len_ = static_cast<uint32_t>(len);

    // Successfully built the packet mbufs. Put it on the socket.
    while (!bufs.is_empty()) {
        auto next = bufs.pop_front();
        if (head_ == nullptr) {
            tail_.push_front(next);
        } else {
            tail_.insert_after(tail_.make_iterator(*head_), next);
        }
        head_ = next;
    }

    *written = len;
    size_ += len;
    return MX_OK;
}

mx_status_t SocketDispatcher::WriteStreamMBufsLocked(user_ptr<const void> src,
                                                     size_t len, size_t* written) {
    if (head_ == nullptr) {
        head_ = AllocMBuf();
        if (head_ == nullptr)
            return MX_ERR_SHOULD_WAIT;
        tail_.push_front(head_);
    }

    size_t pos = 0;
    while (pos < len) {
        if (head_->rem() == 0) {
            auto next = AllocMBuf();
            if (next == nullptr)
                break;
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
        if (src.byte_offset(pos).copy_array_from_user(dst, copy_len) != MX_OK)
            break;
        pos += copy_len;
        head_->len_ += static_cast<uint32_t>(copy_len);
        size_ += copy_len;
    }

    if (pos == 0)
        return MX_ERR_SHOULD_WAIT;

    *written = pos;
    return MX_OK;
}

mx_status_t SocketDispatcher::Read(user_ptr<void> dst, size_t len,
                                   size_t* nread) {
    canary_.Assert();

    AutoLock lock(&lock_);

    // Just query for bytes outstanding.
    if (!dst && len == 0) {
        *nread = size_;
        return MX_OK;
    }

    if (len != (size_t)((uint32_t)len))
        return MX_ERR_INVALID_ARGS;

    bool closed = half_closed_[1] || !other_;

    if (is_empty())
        return closed ? MX_ERR_PEER_CLOSED : MX_ERR_SHOULD_WAIT;

    if (flags_ == MX_SOCKET_DATAGRAM && len > tail_.front().pkt_len_)
        len = tail_.front().pkt_len_;

    bool was_full = is_full();

    auto st = ReadMBufsLocked(dst, len);

    if (is_empty())
        state_tracker_.UpdateState(MX_SOCKET_READABLE, 0u);

    if (!closed && was_full && (st > 0))
        other_->state_tracker_.UpdateState(0u, MX_SOCKET_WRITABLE);

    *nread = static_cast<size_t>(st);
    return MX_OK;
}

size_t SocketDispatcher::ReadMBufsLocked(user_ptr<void> dst, size_t len) {
    size_t pos = 0;
    while (pos < len && !tail_.is_empty()) {
        MBuf& cur = tail_.front();
        char* src = cur.data_ + cur.off_;
        size_t copy_len = MIN(cur.len_, len - pos);
        if (dst.byte_offset(pos).copy_array_to_user(src, copy_len) != MX_OK)
            return pos;
        pos += copy_len;
        cur.off_ += static_cast<uint32_t>(copy_len);
        cur.len_ -= static_cast<uint32_t>(copy_len);
        size_ -= copy_len;
        if (cur.len_ == 0 || flags_ == MX_SOCKET_DATAGRAM) {
            size_ -= cur.len_;
            if (head_ == &cur)
                head_ = nullptr;
            FreeMBuf(tail_.pop_front());
        }
    }
    if (flags_ == MX_SOCKET_DATAGRAM) {
        // Drain any leftover mbufs in the datagram packet.
        while (!tail_.is_empty() && tail_.front().pkt_len_ == 0) {
            MBuf* cur = tail_.pop_front();
            size_ -= cur->len_;
            if (head_ == cur)
                head_ = nullptr;
            FreeMBuf(cur);
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
