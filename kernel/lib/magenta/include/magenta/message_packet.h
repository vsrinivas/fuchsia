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

    ~MessagePacket();

    uint32_t data_size() const { return data_size_; }
    uint32_t num_handles() const { return num_handles_; }

    void set_owns_handles(bool own_handles) { owns_handles_ = own_handles; }

    const void* data() const { return data_.get(); }
    void* mutable_data() { return data_.get(); }
    Handle* const* handles() const { return handles_.get(); }
    Handle** mutable_handles() { return handles_.get(); }

private:
    MessagePacket(uint32_t data_size, uint32_t num_handles);

    bool owns_handles_;
    uint32_t data_size_;
    uint32_t num_handles_;

    // TODO(vtl): Allocate these inline instead.
    mxtl::unique_ptr<uint8_t[]> data_;
    mxtl::unique_ptr<Handle*[]> handles_;
};
