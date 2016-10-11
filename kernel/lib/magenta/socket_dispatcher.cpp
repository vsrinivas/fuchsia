// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/socket_dispatcher.h>

#include <string.h>

#include <assert.h>
#include <err.h>
#include <new.h>
#include <trace.h>
#include <pow2.h>

#include <lib/user_copy/user_ptr.h>

#include <kernel/auto_lock.h>
#include <kernel/vm/vm_aspace.h>
#include <kernel/vm/vm_object.h>

#include <magenta/handle.h>
#include <magenta/io_port_client.h>

#define LOCAL_TRACE 0

constexpr mx_rights_t kDefaultSocketRights =
    MX_RIGHT_TRANSFER | MX_RIGHT_DUPLICATE | MX_RIGHT_READ | MX_RIGHT_WRITE;

constexpr mx_size_t kDeFaultSocketBufferSize = 256 * 1024u;

constexpr mx_signals_t kValidSignalMask =
    MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED |
    MX_SIGNAL_SIGNAL0 | MX_SIGNAL_SIGNAL1 | MX_SIGNAL_SIGNAL2 |
    MX_SIGNAL_SIGNAL3 | MX_SIGNAL_SIGNAL4;

namespace {
// Cribbed from pow2.h, we need overloading to correctly deal with 32 and 64 bits.
template <typename T> T vmodpow2(T val, uint modp2) { return val & ((1U << modp2) - 1); }
}

#define INC_POINTER(len_pow2, ptr, inc) vmodpow2(((ptr) + (inc)), len_pow2)

SocketDispatcher::CBuf::~CBuf() {
    VmAspace::kernel_aspace()->FreeRegion(reinterpret_cast<vaddr_t>(buf_));
}

bool SocketDispatcher::CBuf::Init(uint32_t len) {
    vmo_ = VmObject::Create(PMM_ALLOC_FLAG_ANY, len);
    if (!vmo_)
        return false;

    void* start = nullptr;
    auto st = VmAspace::kernel_aspace()->MapObject(
        vmo_, "socket", 0u, len, &start, PAGE_SIZE_SHIFT, 0,
        0, ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE);

    if (st < 0)
        return false;

    buf_ = reinterpret_cast<char*>(start);

    if (!buf_)
        return false;
    len_pow2_ = log2_uint_floor(len);
    return true;
}

mx_size_t SocketDispatcher::CBuf::free() const {
    uint consumed = modpow2((uint)(head_ - tail_), len_pow2_);
    return valpow2(len_pow2_) - consumed - 1;
}

bool SocketDispatcher::CBuf::empty() const {
    return tail_ == head_;
}

mx_size_t SocketDispatcher::CBuf::Write(const void* src, mx_size_t len, bool from_user) {

    size_t write_len;
    size_t pos = 0;

    while (pos < len && (free() > 0)) {
        if (head_ >= tail_) {
            write_len = MIN(valpow2(len_pow2_) - head_, len - pos);
        } else {
            write_len = MIN(tail_ - head_ - 1, len - pos);
        }

        // if it's full, abort and return how much we've written
        if (write_len == 0) {
            break;
        }

        const char *ptr = (const char*)src;
        ptr += pos;
        if (from_user) {
            // TODO: find a safer way to do this
            user_ptr<const void> uptr(ptr);
            vmo_->WriteUser(uptr, head_, write_len, nullptr);
        } else {
            memcpy(buf_ + head_, ptr, write_len);
        }

        head_ = INC_POINTER(len_pow2_, head_, write_len);
        pos += write_len;
    }
    return pos;
}

mx_size_t SocketDispatcher::CBuf::Read(void* dest, mx_size_t len, bool from_user) {
    size_t ret = 0;

    if (tail_ != head_) {
        size_t pos = 0;
        // loop until we've read everything we need
        // at most this will make two passes to deal with wraparound
        while (pos < len && tail_ != head_) {
            size_t read_len;
            if (head_ > tail_) {
                // simple case where there is no wraparound
                read_len = MIN(head_ - tail_, len - pos);
            } else {
                // read to the end of buffer in this pass
                read_len = MIN(valpow2(len_pow2_) - tail_, len - pos);
            }

            char *ptr = (char*)dest;
            ptr += pos;
            if (from_user) {
                // TODO: find a safer way to do this
                user_ptr<void> uptr(ptr);
                vmo_->ReadUser(uptr, tail_, read_len, nullptr);
            } else {
                memcpy(ptr, buf_ + tail_, read_len);
            }

            tail_ = INC_POINTER(len_pow2_, tail_, read_len);
            pos += read_len;
        }
        ret = pos;
    }
    return ret;
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

SocketDispatcher::SocketDispatcher(uint32_t flags)
    : flags_(flags),
      oob_len_(0u),
      half_closed_{false, false} {
    const auto kSatisfiable =
        MX_SIGNAL_READABLE | MX_SIGNAL_WRITABLE | MX_SIGNAL_PEER_CLOSED | MX_SIGNAL_SIGNALED;
    state_tracker_.set_initial_signals_state(
            mx_signals_state_t{MX_SIGNAL_WRITABLE, kSatisfiable});
}

SocketDispatcher::~SocketDispatcher() {
}

mx_status_t SocketDispatcher::Init(mxtl::RefPtr<SocketDispatcher> other) {
    other_ = mxtl::move(other);
    return cbuf_.Init(kDeFaultSocketBufferSize) ? NO_ERROR : ERR_NO_MEMORY;
}

void SocketDispatcher::on_zero_handles() {
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
    AutoLock lock(&lock_);
    other_.reset();
    state_tracker_.UpdateState(MX_SIGNAL_WRITABLE, MX_SIGNAL_PEER_CLOSED,
                               MX_SIGNAL_WRITABLE, 0u);
    if (iopc_)
        iopc_->Signal(MX_SIGNAL_PEER_CLOSED, &lock_);
}

status_t SocketDispatcher::UserSignal(uint32_t clear_mask, uint32_t set_mask) {
    if ((set_mask & ~MX_SIGNAL_SIGNAL_ALL) || (clear_mask & ~MX_SIGNAL_SIGNAL_ALL))
        return ERR_INVALID_ARGS;

    mxtl::RefPtr<SocketDispatcher> other;
    {
        AutoLock lock(&lock_);
        if (!other_)
            return ERR_REMOTE_CLOSED;
        other = other_;
    }

    return other->UserSignalSelf(clear_mask, set_mask);
}

status_t SocketDispatcher::UserSignalSelf(uint32_t clear_mask, uint32_t set_mask) {
    AutoLock lock(&lock_);
    auto satisfied = state_tracker_.GetSignalsState().satisfied;
    auto changed = ~satisfied & set_mask;

    if (changed) {
        if (iopc_)
            iopc_->Signal(changed, 0u, &lock_);
    }

    state_tracker_.UpdateSatisfied(clear_mask, set_mask);
    return NO_ERROR;
}

status_t SocketDispatcher::set_port_client(mxtl::unique_ptr<IOPortClient> client) {
    if (iopc_)
        return ERR_BAD_STATE;

    if ((client->get_trigger_signals() & ~kValidSignalMask) != 0)
        return ERR_INVALID_ARGS;

    {
        AutoLock lock(&lock_);
        iopc_ = mxtl::move(client);

        if (!cbuf_.empty())
            iopc_->Signal(MX_SIGNAL_READABLE, 0u, &lock_);
    }

    return NO_ERROR;
}

status_t SocketDispatcher::HalfClose() {
    mxtl::RefPtr<SocketDispatcher> other;
    {
        AutoLock lock(&lock_);
        if (half_closed_[0])
            return NO_ERROR;
        if (!other_)
            return ERR_REMOTE_CLOSED;
        other = other_;
        half_closed_[0] = true;
        state_tracker_.UpdateState(MX_SIGNAL_WRITABLE, 0u,
                                   MX_SIGNAL_WRITABLE, 0u);
    }
    return other->HalfCloseOther();
}

status_t SocketDispatcher::HalfCloseOther() {
    AutoLock lock(&lock_);
    half_closed_[1] = true;
    state_tracker_.UpdateSatisfied(0u, MX_SIGNAL_PEER_CLOSED);
    return NO_ERROR;
}

mx_ssize_t SocketDispatcher::WriteHelper(const void* src, mx_size_t len,
                                         bool from_user, bool is_oob) {
    mxtl::RefPtr<SocketDispatcher> other;
    {
        AutoLock lock(&lock_);
        if (!other_)
            return ERR_REMOTE_CLOSED;
        if (half_closed_[0])
            return ERR_BAD_STATE;
        other = other_;
    }

    auto st = is_oob ?
        other->OOB_WriteSelf(src, len, from_user) :
        other->WriteSelf(src, len, from_user);
    return st;
}

mx_ssize_t SocketDispatcher::WriteSelf(const void* src, mx_size_t len, bool from_user) {
    AutoLock lock(&lock_);

    if (!cbuf_.free())
        return ERR_SHOULD_WAIT;

    bool was_empty = cbuf_.empty();

    auto st = cbuf_.Write(src, len, from_user);

    if (st > 0) {
        if (was_empty)
            state_tracker_.UpdateSatisfied(0u, MX_SIGNAL_READABLE);
        if (iopc_)
            iopc_->Signal(MX_SIGNAL_READABLE, st, &lock_);
    }

    if (!cbuf_.free())
        other_->state_tracker_.UpdateSatisfied(MX_SIGNAL_WRITABLE, 0u);

    return st;
}

mx_ssize_t SocketDispatcher::OOB_WriteSelf(const void* src, mx_size_t len, bool from_user) {
    AutoLock lock(&lock_);
    if (oob_len_)
        return ERR_SHOULD_WAIT;
    if (len > sizeof(oob_))
        return ERR_BUFFER_TOO_SMALL;

    if (from_user) {
        if (user_ptr<const void>(src).copy_array_from_user(oob_, len) != NO_ERROR)
            return ERR_INVALID_ARGS;
    } else {
        memcpy(oob_, src, len);
    }

    oob_len_ = len;
    state_tracker_.UpdateSatisfied(0u, MX_SIGNAL_SIGNALED);
    if (iopc_)
        iopc_->Signal(MX_SIGNAL_SIGNALED, &lock_);
    return len;
}

mx_ssize_t SocketDispatcher::Read(void* dest, mx_size_t len, bool from_user) {
    AutoLock lock(&lock_);

    bool closed = half_closed_[1] || !other_;

    if (cbuf_.empty())
        return closed ? ERR_REMOTE_CLOSED: ERR_SHOULD_WAIT;

    bool was_full = cbuf_.free() == 0u;

    auto st = cbuf_.Read(dest, len, from_user);

    mx_signals_t satisfied_clear = 0u;
    mx_signals_t satisfiable_clear = 0u;

    if (cbuf_.empty()) {
        satisfied_clear = MX_SIGNAL_READABLE;
        // If the far end is closed or half closed, no more data will ever arrive.
        if ((st == 0) && closed)
            satisfiable_clear = MX_SIGNAL_READABLE;
    }

    state_tracker_.UpdateState(satisfied_clear, 0u, satisfiable_clear, 0u);

    if (!closed && was_full && (st > 0))
        other_->state_tracker_.UpdateSatisfied(0u, MX_SIGNAL_WRITABLE);

    return st;
}

mx_ssize_t SocketDispatcher::OOB_Read(void* dest, mx_size_t len, bool from_user) {
    AutoLock lock(&lock_);
    if (!oob_len_)
        return ERR_SHOULD_WAIT;
    if (oob_len_ > len)
        return ERR_BUFFER_TOO_SMALL;

    if (from_user) {
        if (user_ptr<void>(dest).copy_array_to_user(oob_, oob_len_) != NO_ERROR)
            return ERR_INVALID_ARGS;
    } else {
        memcpy(dest, oob_, oob_len_);
    }
    auto read_len = oob_len_;
    oob_len_ = 0u;
    state_tracker_.UpdateSatisfied(MX_SIGNAL_SIGNALED, 0u);
    return read_len;
}
