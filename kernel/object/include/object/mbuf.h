// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <lib/user_copy/user_ptr.h>
#include <magenta/types.h>
#include <fbl/intrusive_single_list.h>

class MBufChain {
public:
    MBufChain() = default;
    ~MBufChain();

    mx_status_t WriteStream(user_ptr<const void> src, size_t len, size_t* written);
    mx_status_t WriteDatagram(user_ptr<const void> src, size_t len, size_t* written);
    size_t Read(user_ptr<void> dst, size_t len, bool datagram);
    bool is_full() const;
    bool is_empty() const;
    size_t size() const { return size_; }

private:
    // An MBuf is a small fixed-size chainable memory buffer.
    struct MBuf : public fbl::SinglyLinkedListable<MBuf*> {
        // 8 for the linked list and 4 for the explicit uint32_t fields.
        static constexpr size_t kHeaderSize = 8 + (4 * 4);
        // 16 is for the malloc header.
        static constexpr size_t kMallocSize = 2048 - 16;
        static constexpr size_t kPayloadSize = kMallocSize - kHeaderSize;

        size_t rem() const;

        uint32_t off_ = 0u;
        uint32_t len_ = 0u;
        // pkt_len_ is set to the total number of bytes in a packet
        // when a socket is in MX_SOCKET_DATAGRAM mode. A pkt_len_ of
        // 0 means this mbuf is part of the body of a packet.
        //
        // Always 0 in MX_SOCKET_STREAM mode.
        uint32_t pkt_len_ = 0u;
        uint32_t unused_;
        char data_[kPayloadSize] = {0};
        // TODO: maybe union data_ with char* blocks for large messages
    };
    static_assert(sizeof(MBuf) == MBuf::kMallocSize, "");

    static constexpr size_t kSizeMax = 128 * MBuf::kPayloadSize;

    MBuf* AllocMBuf();
    void FreeMBuf(MBuf* buf);

    fbl::SinglyLinkedList<MBuf*> freelist_;
    fbl::SinglyLinkedList<MBuf*> tail_;
    MBuf* head_ = nullptr;;
    size_t size_ = 0u;
};
