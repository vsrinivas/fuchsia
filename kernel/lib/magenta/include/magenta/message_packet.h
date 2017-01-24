// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <magenta/types.h>
#include <mxtl/intrusive_double_list.h>
#include <mxtl/unique_ptr.h>

class Handle;

class MessagePacket : public mxtl::DoublyLinkedListable<mxtl::unique_ptr<MessagePacket>> {
public:
    // Creates a message packet.
    static mx_status_t Create(uint32_t data_size, uint32_t num_handles,
                              mxtl::unique_ptr<MessagePacket>* msg);

    uint32_t data_size() const { return data_size_; }
    uint32_t num_handles() const { return num_handles_; }

    void set_owns_handles(bool own_handles) { owns_handles_ = own_handles; }

    const void* data() const { return static_cast<void*>(handles_ + num_handles_); }
    void* mutable_data() { return static_cast<void*>(handles_ + num_handles_); }
    Handle* const* handles() const { return handles_; }
    Handle** mutable_handles() { return handles_; }

    // channel calls treat the leading uint32_t of the payload as
    // a transaction id
    uint32_t get_txid() const {
        if (data_size_ < sizeof(uint32_t)) {
            return 0;
        } else {
            return *(reinterpret_cast<const uint32_t*>(data()));
        }
    }

private:
    MessagePacket(uint32_t data_size, uint32_t num_handles, Handle** handles);
    ~MessagePacket();

    static void operator delete(void* ptr) {
        free(ptr);
    }
    friend class mxtl::unique_ptr<MessagePacket>;

    bool owns_handles_;
    uint32_t data_size_;
    uint32_t num_handles_;
    Handle** handles_;
};
