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

#include <kernel/auto_lock.h>
#include <lib/user_copy.h>
#include <magenta/handle.h>

#define LOCAL_TRACE 0

constexpr mx_rights_t kDefaultSocketRights =
    MX_RIGHT_TRANSFER | MX_RIGHT_DUPLICATE | MX_RIGHT_READ | MX_RIGHT_WRITE;
constexpr mx_size_t kDeFaultSocketBufferSize = 64 * 1024u;

namespace {
// Cribbed from pow2.h, we need overloading to correctly deal with 32 and 64 bits.
template <typename T> T vmodpow2(T val, uint modp2) { return val & ((1U << modp2) - 1); }
}

#define INC_POINTER(len_pow2, ptr, inc) vmodpow2(((ptr) + (inc)), len_pow2)

SocketDispatcher::CBuf::~CBuf() {
    free(buf_);
}

bool SocketDispatcher::CBuf::Init(uint32_t len) {
    buf_ = reinterpret_cast<char*>(malloc(len));
    if (!buf_)
        return false;
    len_pow2_ = log2_uint(len);
    return true;
}

mx_size_t SocketDispatcher::CBuf::available() const {
    uint consumed = modpow2((uint)(head_ - tail_), len_pow2_);
    return valpow2(len_pow2_) - consumed - 1;
}

mx_size_t SocketDispatcher::CBuf::Write(const void* src, mx_size_t len, bool from_user) {
    const char *buf = (const char*)src;

    size_t write_len;
    size_t pos = 0;

    while (pos < len && available() > 0) {
        if (head_ >= tail_) {
            write_len = MIN(valpow2(len_pow2_) - head_, len - pos);
        } else {
            write_len = MIN(tail_ - head_ - 1, len - pos);
        }

        // if it's full, abort and return how much we've written
        if (write_len == 0) {
            break;
        }

        if (from_user)
            copy_from_user(buf_ + head_, utils::user_ptr<const char>(buf + pos), write_len);
        else
            memcpy(buf_ + head_, buf + pos, write_len);

        head_ = INC_POINTER(len_pow2_, head_, write_len);
        pos += write_len;
    }

    if (head_ != tail_) {
        // signalled = event_signal(&cbuf->event, false);
    }
    return pos;
}

mx_size_t SocketDispatcher::CBuf::Read(void* dest, mx_size_t len, bool from_user) {
    char *buf = (char*)dest;

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

            if (from_user)
                copy_to_user(utils::user_ptr<char>(buf + pos), buf_ + tail_, read_len);
            else
                memcpy(buf + pos, buf_ + tail_, read_len);

            tail_ = INC_POINTER(len_pow2_, tail_, read_len);
            pos += read_len;
        }

        if (tail_ == head_) {
            DEBUG_ASSERT(pos > 0);
            // we've emptied the buffer, unsignal the event
            // event_unsignal(&cbuf->event);
        }
        ret = pos;
    }
    return ret;
}

// static
status_t SocketDispatcher::Create(uint32_t flags,
                                  utils::RefPtr<Dispatcher>* dispatcher0,
                                  utils::RefPtr<Dispatcher>* dispatcher1,
                                  mx_rights_t* rights) {
    LTRACE_ENTRY;

    AllocChecker ac;
    auto socket0 = utils::AdoptRef(new (&ac) SocketDispatcher(flags));
    if (!ac.check())
        return ERR_NO_MEMORY;

    auto socket1 = utils::AdoptRef(new (&ac) SocketDispatcher(flags));
    if (!ac.check())
        return ERR_NO_MEMORY;

    mx_status_t status;
    if ((status = socket0->Init(socket1)) != NO_ERROR)
        return status;
    if ((status = socket1->Init(socket0)) != NO_ERROR)
        return status;

    *rights = kDefaultSocketRights;
    *dispatcher0 = utils::RefPtr<Dispatcher>(socket0.get());
    *dispatcher1 = utils::RefPtr<Dispatcher>(socket1.get());
    return NO_ERROR;
}

SocketDispatcher::SocketDispatcher(uint32_t flags)
    : flags_(flags) {
    mutex_init(&lock_);
}

SocketDispatcher::~SocketDispatcher() {
    mutex_destroy(&lock_);
}

mx_status_t SocketDispatcher::Init(utils::RefPtr<SocketDispatcher> other) {
    other_ = utils::move(other);
    return cbuf_.Init(kDeFaultSocketBufferSize) ? NO_ERROR : ERR_NO_MEMORY;
}

void SocketDispatcher::on_zero_handles() {
    utils::RefPtr<SocketDispatcher> socket;
    {
        AutoLock lock(&lock_);
        socket = utils::move(other_);
    }
    if (!socket)
        return;

    socket->OnPeerZeroHandles();
}

void SocketDispatcher::OnPeerZeroHandles() {
    {
        AutoLock lock(&lock_);
        other_.reset();
    }
}

mx_ssize_t SocketDispatcher::Write(const void* src, mx_size_t len, bool from_user) {
    utils::RefPtr<SocketDispatcher> socket;
    {
        AutoLock lock(&lock_);
        socket = other_;
    }
    return socket ? socket->WriteSelf(src, len, from_user) : ERR_NOT_READY;
}

mx_ssize_t SocketDispatcher::WriteSelf(const void* src, mx_size_t len, bool from_user) {
    AutoLock lock(&lock_);
    return cbuf_.Write(src, len, from_user);
}

mx_ssize_t SocketDispatcher::Read(void* dest, mx_size_t len, bool from_user) {
    AutoLock lock(&lock_);
    return cbuf_.Read(dest, len, from_user);
}
