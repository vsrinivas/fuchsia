// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <lib/user_copy/user_ptr.h>
#include <zircon/types.h>
#include <fbl/intrusive_single_list.h>

// MBufChain is a container for storing a stream of bytes or a sequence of datagrams.
//
// It's designed to back sockets and channels.  Don't simultaneously store stream data and datagrams
// in a single instance.
class MBufChain {
public:
    MBufChain() = default;
    ~MBufChain();

    // Writes |len| bytes of stream data from |src| and sets |written| to number of bytes written.
    //
    // Returns an error on failure.
    zx_status_t WriteStream(user_in_ptr<const void> src, size_t len, size_t* written);

    // Writes a datagram of |len| bytes from |src| and sets |written| to number of bytes written.
    //
    // This operation is atomic in that either the entire datagram is written successfully or the
    // chain is unmodified.
    //
    // Writing a zero-length datagram is an error.
    //
    // Returns an error on failure.
    zx_status_t WriteDatagram(user_in_ptr<const void> src, size_t len, size_t* written);

    // Reads upto |len| bytes from chain into |dst|.
    //
    // When |datagram| is false, the data in the chain is treated as a stream (no boundaries).
    //
    // When |datagram| is true, the data in the chain is treated as a sequence of datagrams and the
    // call will read at most one datagram.  If |len| is too small to read a complete datagram, a
    // partial datagram is returned and its remaining bytes are discarded.
    //
    // Returns number of bytes read.
    size_t Read(user_out_ptr<void> dst, size_t len, bool datagram);

    bool is_full() const;
    bool is_empty() const;

    // Returns number of bytes stored in the chain.
    size_t size() const { return size_; }

    // Returns the maximum number of bytes that can be stored in the chain.
    size_t max_size() const { return kSizeMax; }

private:
    // An MBuf is a small fixed-size chainable memory buffer.
    struct MBuf : public fbl::SinglyLinkedListable<MBuf*> {
        // 8 for the linked list and 4 for the explicit uint32_t fields.
        static constexpr size_t kHeaderSize = 8 + (4 * 4);
        // 16 is for the malloc header.
        static constexpr size_t kMallocSize = 2048 - 16;
        static constexpr size_t kPayloadSize = kMallocSize - kHeaderSize;

        // Returns number of bytes of free space in this MBuf.
        size_t rem() const;

        uint32_t off_ = 0u;
        uint32_t len_ = 0u;
        // pkt_len_ is set to the total number of bytes in a packet
        // when a socket is in ZX_SOCKET_DATAGRAM mode. A pkt_len_ of
        // 0 means this mbuf is part of the body of a packet.
        //
        // Always 0 in ZX_SOCKET_STREAM mode.
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
