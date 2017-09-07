// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/mbuf.h>

#include <lib/user_copy/user_ptr.h>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

#define LOCAL_TRACE 0

constexpr size_t MBufChain::MBuf::kHeaderSize;
constexpr size_t MBufChain::MBuf::kMallocSize;
constexpr size_t MBufChain::MBuf::kPayloadSize;
constexpr size_t MBufChain::kSizeMax;

size_t MBufChain::MBuf::rem() const {
    return kPayloadSize - (off_ + len_);
}

MBufChain::~MBufChain() {
    while (!tail_.is_empty())
        delete tail_.pop_front();
    while (!freelist_.is_empty())
        delete freelist_.pop_front();
}

bool MBufChain::is_full() const {
    return size_ >= kSizeMax;
}

bool MBufChain::is_empty() const {
    return size_ == 0;
}

size_t MBufChain::Read(user_ptr<void> dst, size_t len, bool datagram) {
    if (datagram && len > tail_.front().pkt_len_)
        len = tail_.front().pkt_len_;

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
        if (cur.len_ == 0 || datagram) {
            size_ -= cur.len_;
            if (head_ == &cur)
                head_ = nullptr;
            FreeMBuf(tail_.pop_front());
        }
    }
    if (datagram) {
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

mx_status_t MBufChain::WriteDatagram(user_ptr<const void> src,
                                     size_t len, size_t* written) {
    if (len + size_ > kSizeMax)
        return MX_ERR_SHOULD_WAIT;

    fbl::SinglyLinkedList<MBuf*> bufs;
    for (size_t need = 1 + ((len - 1) / MBuf::kPayloadSize); need != 0; need--) {
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
        size_t copy_len = fbl::min(MBuf::kPayloadSize, len - pos);
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

mx_status_t MBufChain::WriteStream(user_ptr<const void> src,
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
        size_t copy_len = fbl::min(head_->rem(), len - pos);
        if (size_ + copy_len > kSizeMax) {
            copy_len = kSizeMax - size_;
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

MBufChain::MBuf* MBufChain::AllocMBuf() {
    if (freelist_.is_empty()) {
        fbl::AllocChecker ac;
        MBuf* buf = new (&ac) MBuf();
        return (!ac.check()) ? nullptr : buf;
    }
    return freelist_.pop_front();
}

void MBufChain::FreeMBuf(MBuf* buf) {
    buf->off_ = 0u;
    buf->len_ = 0u;
    freelist_.push_front(buf);
}
