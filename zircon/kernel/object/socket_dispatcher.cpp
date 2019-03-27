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

#include <lib/counters.h>
#include <lib/user_copy/user_ptr.h>

#include <vm/vm_aspace.h>
#include <vm/vm_object.h>
#include <vm/vm_object_paged.h>
#include <object/handle.h>

#include <zircon/rights.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>

#define LOCAL_TRACE 0

KCOUNTER(dispatcher_socket_create_count, "dispatcher.socket.create")
KCOUNTER(dispatcher_socket_destroy_count, "dispatcher.socket.destroy")

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

    ktl::unique_ptr<ControlMsg> control0;
    ktl::unique_ptr<ControlMsg> control1;

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

    auto socket0 = fbl::AdoptRef(new (&ac) SocketDispatcher(ktl::move(holder0), starting_signals,
                                                            flags, ktl::move(control0)));
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    auto socket1 = fbl::AdoptRef(new (&ac) SocketDispatcher(ktl::move(holder1), starting_signals,
                                                            flags, ktl::move(control1)));
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    socket0->Init(socket1);
    socket1->Init(socket0);

    *rights = default_rights();
    *dispatcher0 = ktl::move(socket0);
    *dispatcher1 = ktl::move(socket1);
    return ZX_OK;
}

SocketDispatcher::SocketDispatcher(fbl::RefPtr<PeerHolder<SocketDispatcher>> holder,
                                   zx_signals_t starting_signals, uint32_t flags,
                                   ktl::unique_ptr<ControlMsg> control_msg)
    : PeeredDispatcher(ktl::move(holder), starting_signals),
      flags_(flags),
      control_msg_(ktl::move(control_msg)),
      control_msg_len_(0),
      read_threshold_(0),
      write_threshold_(0),
      read_disabled_(false) {
    kcounter_add(dispatcher_socket_create_count, 1);
}

SocketDispatcher::~SocketDispatcher() {
    kcounter_add(dispatcher_socket_destroy_count, 1);
}

// This is called before either SocketDispatcher is accessible from threads other than the one
// initializing the socket, so it does not need locking.
void SocketDispatcher::Init(fbl::RefPtr<SocketDispatcher> other) TA_NO_THREAD_SAFETY_ANALYSIS {
    peer_ = ktl::move(other);
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

    Guard<fbl::Mutex> guard{get_lock()};

    zx_signals_t signals = GetSignalsStateLocked();
    // If we're already shut down in the requested way, return immediately.
    const uint32_t want_signals =
        (shutdown_read ? ZX_SOCKET_PEER_WRITE_DISABLED : 0) |
        (shutdown_write ? ZX_SOCKET_WRITE_DISABLED : 0);
    const uint32_t have_signals = signals & (ZX_SOCKET_PEER_WRITE_DISABLED | ZX_SOCKET_WRITE_DISABLED);
    if (want_signals == have_signals) {
        return ZX_OK;
    }
    zx_signals_t clear_mask = 0u;
    zx_signals_t set_mask = 0u;
    if (shutdown_read) {
        read_disabled_ = true;
        set_mask |= ZX_SOCKET_PEER_WRITE_DISABLED;
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
        clear_mask |= ZX_SOCKET_WRITABLE;
        set_mask |= ZX_SOCKET_WRITE_DISABLED;
    }
    if (shutdown_write) {
        read_disabled_ = true;
        set_mask |= ZX_SOCKET_PEER_WRITE_DISABLED;
    }

    UpdateStateLocked(clear_mask, set_mask);
    return ZX_OK;
}

zx_status_t SocketDispatcher::Write(Plane plane, user_in_ptr<const void> src, size_t len,
                                    size_t* nwritten) {
    canary_.Assert();

    if (plane == Plane::kData) {
        return WriteData(src, len, nwritten);
    } else {
        zx_status_t status = WriteControl(src, len);

        // No partial control messages, on success we wrote everything.
        if (status == ZX_OK) {
            *nwritten = len;
        }

        return status;
    }
}

zx_status_t SocketDispatcher::WriteData(user_in_ptr<const void> src, size_t len,
                                        size_t* nwritten) TA_NO_THREAD_SAFETY_ANALYSIS {
    canary_.Assert();

    LTRACE_ENTRY;

    Guard<fbl::Mutex> guard{get_lock()};

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

    Guard<fbl::Mutex> guard{get_lock()};
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

    zx_signals_t clear = 0u;
    zx_signals_t set = 0u;

    if (st > 0) {
        if (was_empty)
            set |= ZX_SOCKET_READABLE;
        // Assert signal if we go above the read threshold
        if ((read_threshold_ > 0) && (data_.size() >= read_threshold_))
            set |= ZX_SOCKET_READ_THRESHOLD;
        if (set) {
            UpdateStateLocked(0u, set);
        }
        if (peer_) {
            size_t peer_write_threshold = peer_->write_threshold_;
            // If free space falls below threshold, de-signal
            if ((peer_write_threshold > 0) &&
                ((data_.max_size() - data_.size()) < peer_write_threshold))
                clear |= ZX_SOCKET_WRITE_THRESHOLD;
        }
    }

    if (peer_ && is_full())
        clear |= ZX_SOCKET_WRITABLE;

    if (clear)
        peer_->UpdateStateLocked(clear, 0u);

    *written = st;
    return status;
}

zx_status_t SocketDispatcher::Read(Plane plane, ReadType type, user_out_ptr<void> dst, size_t len,
                                   size_t* nread) {
    canary_.Assert();

    if (plane == Plane::kData) {
        return ReadData(type, dst, len, nread);
    } else {
        return ReadControl(type, dst, len, nread);
    }
}

zx_status_t SocketDispatcher::ReadData(ReadType type, user_out_ptr<void> dst, size_t len,
                                       size_t* nread) TA_NO_THREAD_SAFETY_ANALYSIS {
    canary_.Assert();

    LTRACE_ENTRY;

    Guard<fbl::Mutex> guard{get_lock()};

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

    size_t st = 0;
    if (type == ReadType::kPeek) {
        st = data_.Peek(dst, len, flags_ & ZX_SOCKET_DATAGRAM);
    } else {
        bool was_full = is_full();

        st = data_.Read(dst, len, flags_ & ZX_SOCKET_DATAGRAM);

        zx_signals_t clear = 0u;
        zx_signals_t set = 0u;

        // Deassert signal if we fell below the read threshold
        if ((read_threshold_ > 0) && (data_.size() < read_threshold_))
            clear |= ZX_SOCKET_READ_THRESHOLD;

        if (is_empty()) {
            clear |= ZX_SOCKET_READABLE;
        }
        if (set || clear) {
            UpdateStateLocked(clear, set);
            clear = set = 0u;
        }
        if (peer_) {
            // Assert (write threshold) signal if space available is above
            // threshold.
            size_t peer_write_threshold = peer_->write_threshold_;
            if (peer_write_threshold > 0 &&
                ((data_.max_size() - data_.size()) >= peer_write_threshold))
                set |= ZX_SOCKET_WRITE_THRESHOLD;
            if (was_full && (st > 0))
                set |= ZX_SOCKET_WRITABLE;
            if (set)
                peer_->UpdateStateLocked(0u, set);
        }
    }

    *nread = static_cast<size_t>(st);
    return ZX_OK;
}

zx_status_t SocketDispatcher::ReadControl(ReadType type, user_out_ptr<void> dst, size_t len,
                                          size_t* nread) TA_NO_THREAD_SAFETY_ANALYSIS {
    canary_.Assert();

    if ((flags_ & ZX_SOCKET_HAS_CONTROL) == 0) {
        return ZX_ERR_BAD_STATE;
    }

    Guard<fbl::Mutex> guard{get_lock()};

    if (control_msg_len_ == 0)
        return ZX_ERR_SHOULD_WAIT;

    size_t copy_len = MIN(control_msg_len_, len);
    if (dst.copy_array_to_user(&control_msg_->msg, copy_len) != ZX_OK)
        return ZX_ERR_INVALID_ARGS; // Invalid user buffer.

    if (type == ReadType::kConsume) {
        control_msg_len_ = 0;
        UpdateStateLocked(ZX_SOCKET_CONTROL_READABLE, 0u);
        if (peer_)
            peer_->UpdateStateLocked(0u, ZX_SOCKET_CONTROL_WRITABLE);
    }

    *nread = copy_len;
    return ZX_OK;
}

zx_status_t SocketDispatcher::CheckShareable(SocketDispatcher* to_send) {
    // We disallow sharing of sockets that support sharing themselves
    // and disallow sharing either end of the socket we're going to
    // share on, thus preventing loops, etc.
    Guard<fbl::Mutex> guard{get_lock()};
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

    Guard<fbl::Mutex> guard{get_lock()};
    if (!peer_)
        return ZX_ERR_PEER_CLOSED;

    return peer_->ShareSelfLocked(ktl::move(h));
}

zx_status_t SocketDispatcher::ShareSelfLocked(HandleOwner h) TA_NO_THREAD_SAFETY_ANALYSIS {
    canary_.Assert();

    if (accept_queue_)
        return ZX_ERR_SHOULD_WAIT;

    accept_queue_ = ktl::move(h);

    UpdateStateLocked(0, ZX_SOCKET_ACCEPT);
    if (peer_)
        peer_->UpdateStateLocked(ZX_SOCKET_SHARE, 0);

    return ZX_OK;
}

zx_status_t SocketDispatcher::Accept(HandleOwner* h) TA_NO_THREAD_SAFETY_ANALYSIS {
    canary_.Assert();

    if (!(flags_ & ZX_SOCKET_HAS_ACCEPT))
        return ZX_ERR_NOT_SUPPORTED;

    Guard<fbl::Mutex> guard{get_lock()};

    if (!accept_queue_)
        return ZX_ERR_SHOULD_WAIT;

    *h = ktl::move(accept_queue_);

    UpdateStateLocked(ZX_SOCKET_ACCEPT, 0);
    if (peer_)
        peer_->UpdateStateLocked(0, ZX_SOCKET_SHARE);

    return ZX_OK;
}

// NOTE(abdulla): peer_ is protected by get_lock() while peer_->data_
// is protected by peer_->get_lock(). These two locks are aliases of
// one another so must only acquire one of them. Thread-safety
// analysis does not know they are the same lock so we must disable
// analysis.
void SocketDispatcher::GetInfo(zx_info_socket_t* info) const TA_NO_THREAD_SAFETY_ANALYSIS {
    canary_.Assert();
    Guard<fbl::Mutex> guard{get_lock()};
    *info = zx_info_socket_t{
        .options = flags_,
        .rx_buf_max = data_.max_size(),
        .rx_buf_size = data_.size(),
        .rx_buf_available = data_.size(flags_ & ZX_SOCKET_DATAGRAM),
        .tx_buf_max = peer_ ? peer_->data_.max_size() : 0,
        .tx_buf_size = peer_ ? peer_->data_.size() : 0,
    };
}

size_t SocketDispatcher::GetReadThreshold() const TA_NO_THREAD_SAFETY_ANALYSIS {
    canary_.Assert();
    Guard<fbl::Mutex> guard{get_lock()};
    return read_threshold_;
}

size_t SocketDispatcher::GetWriteThreshold() const TA_NO_THREAD_SAFETY_ANALYSIS {
    canary_.Assert();
    Guard<fbl::Mutex> guard{get_lock()};
    return write_threshold_;
}

zx_status_t SocketDispatcher::SetReadThreshold(size_t value) TA_NO_THREAD_SAFETY_ANALYSIS {
    canary_.Assert();
    Guard<fbl::Mutex> guard{get_lock()};
    if (value > data_.max_size())
        return ZX_ERR_INVALID_ARGS;
    read_threshold_ = value;
    // Setting 0 disables thresholding. Deassert signal unconditionally.
    if (value == 0) {
        UpdateStateLocked(ZX_SOCKET_READ_THRESHOLD, 0u);
    } else {
        if (data_.size() >= read_threshold_) {
            // Assert signal if we have queued data above the read threshold
            UpdateStateLocked(0u, ZX_SOCKET_READ_THRESHOLD);
        } else {
            // De-assert signal if we upped threshold and queued data drops below
            UpdateStateLocked(ZX_SOCKET_READ_THRESHOLD, 0u);
        }
    }
    return ZX_OK;
}

zx_status_t SocketDispatcher::SetWriteThreshold(size_t value) TA_NO_THREAD_SAFETY_ANALYSIS {
    canary_.Assert();
    Guard<fbl::Mutex> guard{get_lock()};
    if (peer_ == NULL)
        return ZX_ERR_PEER_CLOSED;
    if (value > peer_->data_.max_size())
        return ZX_ERR_INVALID_ARGS;
    write_threshold_ = value;
    // Setting 0 disables thresholding. Deassert signal unconditionally.
    if (value == 0) {
        UpdateStateLocked(ZX_SOCKET_WRITE_THRESHOLD, 0u);
    } else {
        // Assert signal if we have available space above the write threshold
        if ((peer_->data_.max_size() - peer_->data_.size()) >= write_threshold_) {
            // Assert signal if we have available space above the write threshold
            UpdateStateLocked(0u, ZX_SOCKET_WRITE_THRESHOLD);
        } else {
            // De-assert signal if we upped threshold and available space drops below
            UpdateStateLocked(ZX_SOCKET_WRITE_THRESHOLD, 0u);
        }
    }
    return ZX_OK;
}
