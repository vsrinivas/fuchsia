// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <lib/user_copy/user_ptr.h>
#include <magenta/types.h>
#include <mxtl/intrusive_single_list.h>

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
