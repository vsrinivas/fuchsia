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

// static
status_t SocketDispatcher::Create(uint32_t flags,
                                  mxtl::RefPtr<Dispatcher>* dispatcher0,
                                  mxtl::RefPtr<Dispatcher>* dispatcher1,
                                  mx_rights_t* rights) {
    LTRACE_ENTRY;

    if (flags != MX_SOCKET_STREAM && flags != MX_SOCKET_DATAGRAM) {
        return MX_ERR_INVALID_ARGS;
    }

    AllocChecker ac;
    auto socket0 = mxtl::AdoptRef(new (&ac) SocketDispatcher(flags));
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    auto socket1 = mxtl::AdoptRef(new (&ac) SocketDispatcher(flags));
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    socket0->Init(socket1);
    socket1->Init(socket0);

    *rights = MX_DEFAULT_SOCKET_RIGHTS;
    *dispatcher0 = mxtl::move(socket0);
    *dispatcher1 = mxtl::move(socket1);
    return MX_OK;
}

SocketDispatcher::SocketDispatcher(uint32_t flags)
    : flags_(flags),
      peer_koid_(0u),
      state_tracker_(MX_SOCKET_WRITABLE) {
}

SocketDispatcher::~SocketDispatcher() {
}

// This is called before either SocketDispatcher is accessible from threads other than the one
// initializing the socket, so it does not need locking.
void SocketDispatcher::Init(mxtl::RefPtr<SocketDispatcher> other) TA_NO_THREAD_SAFETY_ANALYSIS {
    other_ = mxtl::move(other);
    peer_koid_ = other_->get_koid();
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

status_t SocketDispatcher::Shutdown(uint32_t how) {
    canary_.Assert();

    LTRACE_ENTRY;

    const bool shutdown_read = how & MX_SOCKET_SHUTDOWN_READ;
    const bool shutdown_write = how & MX_SOCKET_SHUTDOWN_WRITE;

    mxtl::RefPtr<SocketDispatcher> other;
    {
        AutoLock lock(&lock_);
        mx_signals_t signals = state_tracker_.GetSignalsState();
        // If we're already shut down in the requested way, return immediately.
        const uint32_t want_signals =
            (shutdown_read ? MX_SOCKET_READ_DISABLED : 0) |
            (shutdown_write ? MX_SOCKET_WRITE_DISABLED : 0);
        const uint32_t have_signals = signals & (MX_SOCKET_READ_DISABLED | MX_SOCKET_WRITE_DISABLED);
        if (want_signals == have_signals) {
            return MX_OK;
        }
        other = other_;
        mx_signals_t clear_mask = 0u;
        mx_signals_t set_mask = 0u;
        if (shutdown_read) {
            clear_mask |= MX_SOCKET_READABLE;
            set_mask |= MX_SOCKET_READ_DISABLED;
        }
        if (shutdown_write) {
            clear_mask |= MX_SOCKET_WRITABLE;
            set_mask |= MX_SOCKET_WRITE_DISABLED;
        }
        state_tracker_.UpdateState(clear_mask, set_mask);
    }
    // Our peer already be closed - if so, we've already updated our own bits so we are done. If the
    // peer is done, we need to notify them of the state change.
    if (other) {
        return other->ShutdownOther(how);
    } else {
        return MX_OK;
    }
}

status_t SocketDispatcher::ShutdownOther(uint32_t how) {
    canary_.Assert();

    const bool shutdown_read = how & MX_SOCKET_SHUTDOWN_READ;
    const bool shutdown_write = how & MX_SOCKET_SHUTDOWN_WRITE;

    AutoLock lock(&lock_);
    mx_signals_t clear_mask = 0u;
    mx_signals_t set_mask = 0u;
    if (shutdown_read) {
        // If the other end shut down reading, we can't write any more.
        clear_mask |= MX_SOCKET_WRITABLE;
        set_mask |= MX_SOCKET_WRITE_DISABLED;
    }
    if (shutdown_write) {
        // If the other end shut down writing, we can't read any more than already exists in the
        // buffer. Read() will clear the MX_SOCKET_READABLE bit when the socket is empty.
        set_mask |= MX_SOCKET_READ_DISABLED;
    }

    state_tracker_.UpdateState(clear_mask, set_mask);
    return MX_OK;
}

mx_status_t SocketDispatcher::Write(user_ptr<const void> src, size_t len,
                                    size_t* nwritten) {
    canary_.Assert();

    LTRACE_ENTRY;

    mxtl::RefPtr<SocketDispatcher> other;
    {
        AutoLock lock(&lock_);
        if (!other_)
            return MX_ERR_PEER_CLOSED;
        mx_signals_t signals = state_tracker_.GetSignalsState();
        if (signals & MX_SOCKET_WRITE_DISABLED)
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
        status = data_.WriteDgramMBufs(src, len, &st);
    } else {
        status = data_.WriteStreamMBufs(src, len, &st);
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

mx_status_t SocketDispatcher::Read(user_ptr<void> dst, size_t len,
                                   size_t* nread) {
    canary_.Assert();

    LTRACE_ENTRY;

    AutoLock lock(&lock_);

    // Just query for bytes outstanding.
    if (!dst && len == 0) {
        *nread = data_.size();
        return MX_OK;
    }

    if (len != (size_t)((uint32_t)len))
        return MX_ERR_INVALID_ARGS;

    mx_signals_t signals = state_tracker_.GetSignalsState();
    const bool read_disabled = signals & MX_SOCKET_READ_DISABLED;

    if (is_empty()) {
        if (!other_)
            return MX_ERR_PEER_CLOSED;
        // If reading is disabled on our end or writing is disabled on our peer, we'll never
        // become readable again. Return a different error to let the caller know.
        if (read_disabled)
            return MX_ERR_BAD_STATE;
        return MX_ERR_SHOULD_WAIT;
    }

    bool was_full = is_full();

    auto st = data_.ReadMBufs(dst, len, flags_ == MX_SOCKET_DATAGRAM);

    if (is_empty())
        state_tracker_.UpdateState(MX_SOCKET_READABLE, 0u);

    if (other_ && !read_disabled && was_full && (st > 0))
        other_->state_tracker_.UpdateState(0u, MX_SOCKET_WRITABLE);

    *nread = static_cast<size_t>(st);
    return MX_OK;
}
