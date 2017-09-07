// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <lib/user_copy/user_ptr.h>
#include <magenta/types.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/unique_ptr.h>

constexpr uint32_t kMaxMessageSize = 65536u;
constexpr uint32_t kMaxMessageHandles = 64u;

// ensure public constants are aligned
static_assert(MX_CHANNEL_MAX_MSG_BYTES == kMaxMessageSize, "");
static_assert(MX_CHANNEL_MAX_MSG_HANDLES == kMaxMessageHandles, "");

class Handle;

class MessagePacket : public fbl::DoublyLinkedListable<fbl::unique_ptr<MessagePacket>> {
public:
    // Creates a message packet containing the provided data and space for
    // |num_handles| handles. The handles array is uninitialized and must
    // be completely overwritten by clients.
    static mx_status_t Create(user_ptr<const void> data, uint32_t data_size,
                              uint32_t num_handles,
                              fbl::unique_ptr<MessagePacket>* msg);
    static mx_status_t Create(const void* data, uint32_t data_size,
                              uint32_t num_handles,
                              fbl::unique_ptr<MessagePacket>* msg);

    uint32_t data_size() const { return data_size_; }

    // Copies the packet's |data_size()| bytes to |buf|.
    // Returns an error if |buf| points to a bad user address.
    mx_status_t CopyDataTo(user_ptr<void> buf) const {
        return buf.copy_array_to_user(data(), data_size_);
    }

    uint32_t num_handles() const { return num_handles_; }
    Handle* const* handles() const { return handles_; }
    Handle** mutable_handles() { return handles_; }

    void set_owns_handles(bool own_handles) { owns_handles_ = own_handles; }

    // mx_channel_call treats the leading bytes of the payload as
    // a transaction id of type mx_txid_t.
    mx_txid_t get_txid() const {
        if (data_size_ < sizeof(mx_txid_t)) {
            return 0;
        } else {
            return *(reinterpret_cast<const mx_txid_t*>(data()));
        }
    }

private:
    MessagePacket(uint32_t data_size, uint32_t num_handles, Handle** handles);
    ~MessagePacket();

    // Allocates a new packet that can hold the specified amount of
    // data/handles.
    static mx_status_t NewPacket(uint32_t data_size, uint32_t num_handles,
                                 fbl::unique_ptr<MessagePacket>* msg);

    // Create() uses malloc(), so we must delete using free().
    static void operator delete(void* ptr) {
        free(ptr);
    }
    friend class fbl::unique_ptr<MessagePacket>;

    // Handles and data are stored in the same buffer: num_handles_ Handle*
    // entries first, then the data buffer.
    void* data() const { return static_cast<void*>(handles_ + num_handles_); }

    Handle** const handles_;
    const uint32_t data_size_;
    const uint16_t num_handles_;
    bool owns_handles_;
};
