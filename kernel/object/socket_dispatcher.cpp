// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/socket_dispatcher.h>

#include <string.h>

#include <assert.h>
#include <err.h>
#include <pow2.h>
#include <trace.h>

#include <lib/user_copy/user_ptr.h>

#include <vm/vm_aspace.h>
#include <vm/vm_object.h>
#include <vm/vm_object_paged.h>
#include <object/handle.h>

#include <zircon/rights.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>

using fbl::AutoLock;

#define LOCAL_TRACE 0

// static
zx_status_t SocketDispatcher::Create(uint32_t flags,
                                     fbl::RefPtr<Dispatcher>* dispatcher0,
                                     fbl::RefPtr<Dispatcher>* dispatcher1,
                                     zx_rights_t* rights) {
    LTRACE_ENTRY;

    if (flags & ~ZX_SOCKET_CREATE_MASK)
        return ZX_ERR_INVALID_ARGS;

    fbl::AllocChecker ac;

    zx_signals_t starting_signals = ZX_SOCKET_WRITABLE;

    if (flags & ZX_SOCKET_HAS_ACCEPT)
        starting_signals |= ZX_SOCKET_SHARE;

    fbl::unique_ptr<ControlMsg> control0;
    fbl::unique_ptr<ControlMsg> control1;

    // TODO: use mbufs to avoid pinning control buffer memory.
    if (flags & ZX_SOCKET_HAS_CONTROL) {
        starting_signals |= ZX_SOCKET_CONTROL_WRITABLE;

        control0.reset(new (&ac) ControlMsg());
        if (!ac.check())
            return ZX_ERR_NO_MEMORY;

        control1.reset(new (&ac) ControlMsg());
        if (!ac.check())
            return ZX_ERR_NO_MEMORY;
    }

    auto holder0 = fbl::AdoptRef(new (&ac) PeerHolder<SocketDispatcher>());
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;
    auto holder1 = holder0;

    auto socket0 = fbl::AdoptRef(new (&ac) SocketDispatcher(fbl::move(holder0), starting_signals,
                                                            flags, fbl::move(control0)));
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    auto socket1 = fbl::AdoptRef(new (&ac) SocketDispatcher(fbl::move(holder1), starting_signals,
                                                            flags, fbl::move(control1)));
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    socket0->Init(socket1);
    socket1->Init(socket0);

    *rights = ZX_DEFAULT_SOCKET_RIGHTS;
    *dispatcher0 = fbl::move(socket0);
    *dispatcher1 = fbl::move(socket1);
    return ZX_OK;
}

SocketDispatcher::SocketDispatcher(fbl::RefPtr<PeerHolder<SocketDispatcher>> holder,
                                   zx_signals_t starting_signals, uint32_t flags,
                                   fbl::unique_ptr<ControlMsg> control_msg)
    : PeeredDispatcher(fbl::move(holder), starting_signals),
      flags_(flags),
      control_msg_(fbl::move(control_msg)),
      control_msg_len_(0),
      read_disabled_(false) {
}

SocketDispatcher::~SocketDispatcher() {
}

// This is called before either SocketDispatcher is accessible from threads other than the one
// initializing the socket, so it does not need locking.
void SocketDispatcher::Init(fbl::RefPtr<SocketDispatcher> other) TA_NO_THREAD_SAFETY_ANALYSIS {
    peer_ = fbl::move(other);
    peer_koid_ = peer_->get_koid();
}

void SocketDispatcher::on_zero_handles_locked() {
    canary_.Assert();
}

void SocketDispatcher::OnPeerZeroHandlesLocked() {
    canary_.Assert();

    UpdateStateLocked(ZX_SOCKET_WRITABLE, ZX_SOCKET_PEER_CLOSED);
}

zx_status_t SocketDispatcher::UserSignalSelfLocked(uint32_t clear_mask, uint32_t set_mask) {
    canary_.Assert();
    UpdateStateLocked(clear_mask, set_mask);
    return ZX_OK;
}

zx_status_t SocketDispatcher::Shutdown(uint32_t how) TA_NO_THREAD_SAFETY_ANALYSIS {
    canary_.Assert();

    LTRACE_ENTRY;

    const bool shutdown_read = how & ZX_SOCKET_SHUTDOWN_READ;
    const bool shutdown_write = how & ZX_SOCKET_SHUTDOWN_WRITE;

    AutoLock lock(get_lock());

    zx_signals_t signals = GetSignalsStateLocked();
    // If we're already shut down in the requested way, return immediately.
    const uint32_t want_signals =
        (shutdown_read ? ZX_SOCKET_READ_DISABLED : 0) |
        (shutdown_write ? ZX_SOCKET_WRITE_DISABLED : 0);
    const uint32_t have_signals = signals & (ZX_SOCKET_READ_DISABLED | ZX_SOCKET_WRITE_DISABLED);
    if (want_signals == have_signals) {
        return ZX_OK;
    }
    zx_signals_t clear_mask = 0u;
    zx_signals_t set_mask = 0u;
    if (shutdown_read) {
        read_disabled_ = true;
        if (is_empty())
            set_mask |= ZX_SOCKET_READ_DISABLED;
    }
    if (shutdown_write) {
        clear_mask |= ZX_SOCKET_WRITABLE;
        set_mask |= ZX_SOCKET_WRITE_DISABLED;
    }
    UpdateStateLocked(clear_mask, set_mask);

    // Our peer already be closed - if so, we've already updated our own bits so we are done. If the
    // peer is done, we need to notify them of the state change.
    if (peer_ != nullptr) {
        return peer_->ShutdownOtherLocked(how);
    } else {
        return ZX_OK;
    }
}

zx_status_t SocketDispatcher::ShutdownOtherLocked(uint32_t how) {
    canary_.Assert();

    const bool shutdown_read = how & ZX_SOCKET_SHUTDOWN_READ;
    const bool shutdown_write = how & ZX_SOCKET_SHUTDOWN_WRITE;

    zx_signals_t clear_mask = 0u;
    zx_signals_t set_mask = 0u;
    if (shutdown_read) {
        // If the other end shut down reading, we can't write any more.
        clear_mask |= ZX_SOCKET_WRITABLE;
        set_mask |= ZX_SOCKET_WRITE_DISABLED;
    }
    if (shutdown_write) {
        // If the other end shut down writing, we can't read any more than already exists in the
        // buffer. If we're empty, set ZX_SOCKET_READ_DISABLED now. If we aren't empty, Read() will
        // set this bit after reading the remaining data from the socket.
        read_disabled_ = true;
        if (is_empty())
            set_mask |= ZX_SOCKET_READ_DISABLED;
    }

    UpdateStateLocked(clear_mask, set_mask);
    return ZX_OK;
}

zx_status_t SocketDispatcher::Write(user_in_ptr<const void> src, size_t len,
                                    size_t* nwritten) TA_NO_THREAD_SAFETY_ANALYSIS {
    canary_.Assert();

    LTRACE_ENTRY;

    AutoLock lock(get_lock());

    if (!peer_)
        return ZX_ERR_PEER_CLOSED;
    zx_signals_t signals = GetSignalsStateLocked();
    if (signals & ZX_SOCKET_WRITE_DISABLED)
        return ZX_ERR_BAD_STATE;

    if (len == 0) {
        *nwritten = 0;
        return ZX_OK;
    }
    if (len != static_cast<size_t>(static_cast<uint32_t>(len)))
        return ZX_ERR_INVALID_ARGS;

    return peer_->WriteSelfLocked(src, len, nwritten);
}

zx_status_t SocketDispatcher::WriteControl(user_in_ptr<const void> src, size_t len)
    TA_NO_THREAD_SAFETY_ANALYSIS {
    canary_.Assert();

    if ((flags_ & ZX_SOCKET_HAS_CONTROL) == 0)
        return ZX_ERR_BAD_STATE;

    if (len == 0)
        return ZX_ERR_INVALID_ARGS;

    if (len > ControlMsg::kSize)
        return ZX_ERR_OUT_OF_RANGE;

    AutoLock lock(get_lock());
    if (!peer_)
        return ZX_ERR_PEER_CLOSED;

    return peer_->WriteControlSelfLocked(src, len);
}

zx_status_t SocketDispatcher::WriteControlSelfLocked(user_in_ptr<const void> src,
                                                     size_t len) TA_NO_THREAD_SAFETY_ANALYSIS {
    canary_.Assert();

    if (control_msg_len_ != 0)
        return ZX_ERR_SHOULD_WAIT;

    if (src.copy_array_from_user(&control_msg_->msg, len) != ZX_OK)
        return ZX_ERR_INVALID_ARGS; // Bad user buffer.

    control_msg_len_ = static_cast<uint32_t>(len);

    UpdateStateLocked(0u, ZX_SOCKET_CONTROL_READABLE);
    if (peer_)
        peer_->UpdateStateLocked(ZX_SOCKET_CONTROL_WRITABLE, 0u);

    return ZX_OK;
}

zx_status_t SocketDispatcher::WriteSelfLocked(user_in_ptr<const void> src, size_t len,
                                              size_t* written) TA_NO_THREAD_SAFETY_ANALYSIS {
    canary_.Assert();

    if (is_full())
        return ZX_ERR_SHOULD_WAIT;

    bool was_empty = is_empty();

    size_t st = 0u;
    zx_status_t status;
    if (flags_ & ZX_SOCKET_DATAGRAM) {
        status = data_.WriteDatagram(src, len, &st);
    } else {
        status = data_.WriteStream(src, len, &st);
    }
    if (status)
        return status;

    if (st > 0) {
        if (was_empty)
            UpdateStateLocked(0u, ZX_SOCKET_READABLE);
    }

    if (peer_ && is_full())
        peer_->UpdateStateLocked(ZX_SOCKET_WRITABLE, 0u);

    *written = st;
    return status;
}

zx_status_t SocketDispatcher::Read(user_out_ptr<void> dst, size_t len,
                                   size_t* nread) TA_NO_THREAD_SAFETY_ANALYSIS {
    canary_.Assert();

    LTRACE_ENTRY;

    AutoLock lock(get_lock());

    // Just query for bytes outstanding.
    if (!dst && len == 0) {
        *nread = data_.size();
        return ZX_OK;
    }

    if (len != (size_t)((uint32_t)len))
        return ZX_ERR_INVALID_ARGS;

    if (is_empty()) {
        if (!peer_)
            return ZX_ERR_PEER_CLOSED;
        // If reading is disabled on our end and we're empty, we'll never become readable again.
        // Return a different error to let the caller know.
        if (read_disabled_)
            return ZX_ERR_BAD_STATE;
        return ZX_ERR_SHOULD_WAIT;
    }

    bool was_full = is_full();

    auto st = data_.Read(dst, len, flags_ & ZX_SOCKET_DATAGRAM);

    if (is_empty()) {
        uint32_t set_mask = 0u;
        if (read_disabled_)
            set_mask |= ZX_SOCKET_READ_DISABLED;
        UpdateStateLocked(ZX_SOCKET_READABLE, set_mask);
    }

    if (peer_ && was_full && (st > 0))
        peer_->UpdateStateLocked(0u, ZX_SOCKET_WRITABLE);

    *nread = static_cast<size_t>(st);
    return ZX_OK;
}

zx_status_t SocketDispatcher::ReadControl(user_out_ptr<void> dst, size_t len,
                                          size_t* nread) TA_NO_THREAD_SAFETY_ANALYSIS {
    canary_.Assert();

    if ((flags_ & ZX_SOCKET_HAS_CONTROL) == 0) {
        return ZX_ERR_BAD_STATE;
    }

    AutoLock lock(get_lock());

    if (control_msg_len_ == 0)
        return ZX_ERR_SHOULD_WAIT;

    size_t copy_len = MIN(control_msg_len_, len);
    if (dst.copy_array_to_user(&control_msg_->msg, copy_len) != ZX_OK)
        return ZX_ERR_INVALID_ARGS; // Invalid user buffer.

    control_msg_len_ = 0;
    UpdateStateLocked(ZX_SOCKET_CONTROL_READABLE, 0u);
    if (peer_)
        peer_->UpdateStateLocked(0u, ZX_SOCKET_CONTROL_WRITABLE);

    *nread = copy_len;
    return ZX_OK;
}

zx_status_t SocketDispatcher::CheckShareable(SocketDispatcher* to_send) {
    // We disallow sharing of sockets that support sharing themselves
    // and disallow sharing either end of the socket we're going to
    // share on, thus preventing loops, etc.
    AutoLock lock(get_lock());
    if ((to_send->flags_ & ZX_SOCKET_HAS_ACCEPT) ||
        (to_send == this) || (to_send == peer_.get()))
        return ZX_ERR_BAD_STATE;
    return ZX_OK;
}

zx_status_t SocketDispatcher::Share(HandleOwner h) TA_NO_THREAD_SAFETY_ANALYSIS {
    canary_.Assert();

    LTRACE_ENTRY;

    if (!(flags_ & ZX_SOCKET_HAS_ACCEPT))
        return ZX_ERR_NOT_SUPPORTED;

    AutoLock lock(get_lock());
    if (!peer_)
        return ZX_ERR_PEER_CLOSED;

    return peer_->ShareSelfLocked(fbl::move(h));
}

zx_status_t SocketDispatcher::ShareSelfLocked(HandleOwner h) TA_NO_THREAD_SAFETY_ANALYSIS {
    canary_.Assert();

    if (accept_queue_)
        return ZX_ERR_SHOULD_WAIT;

    accept_queue_ = fbl::move(h);

    UpdateStateLocked(0, ZX_SOCKET_ACCEPT);
    if (peer_)
        peer_->UpdateStateLocked(ZX_SOCKET_SHARE, 0);

    return ZX_OK;
}

zx_status_t SocketDispatcher::Accept(HandleOwner* h) TA_NO_THREAD_SAFETY_ANALYSIS {
    canary_.Assert();

    if (!(flags_ & ZX_SOCKET_HAS_ACCEPT))
        return ZX_ERR_NOT_SUPPORTED;

    AutoLock lock(get_lock());

    if (!accept_queue_)
        return ZX_ERR_SHOULD_WAIT;

    *h = fbl::move(accept_queue_);

    UpdateStateLocked(ZX_SOCKET_ACCEPT, 0);
    if (peer_)
        peer_->UpdateStateLocked(0, ZX_SOCKET_SHARE);

    return ZX_OK;
}

size_t SocketDispatcher::ReceiveBufferMax() const {
    canary_.Assert();
    AutoLock lock(get_lock());
    return data_.max_size();
}

size_t SocketDispatcher::ReceiveBufferSize() const {
    canary_.Assert();
    AutoLock lock(get_lock());
    return data_.size();
}

// NOTE(abdulla): peer_ is protected by get_lock() while peer_->data_
// is protected by peer_->get_lock(). These two locks are aliases of
// one another so must only acquire one of them. Thread-safety
// analysis does not know they are the same lock so we must disable
// analysis.
size_t SocketDispatcher::TransmitBufferMax() const TA_NO_THREAD_SAFETY_ANALYSIS {
    canary_.Assert();
    AutoLock lock(get_lock());
    return peer_ ? peer_->data_.max_size() : 0;
}

size_t SocketDispatcher::TransmitBufferSize() const TA_NO_THREAD_SAFETY_ANALYSIS {
    canary_.Assert();
    AutoLock lock(get_lock());
    return peer_ ? peer_->data_.size() : 0;
}
