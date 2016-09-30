// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/message_packet.h>

#include <err.h>
#include <new.h>

#include <magenta/magenta.h>

// static
mx_status_t MessagePacket::Create(uint32_t data_size, uint32_t num_handles,
                                  mxtl::unique_ptr<MessagePacket>* msg) {
    AllocChecker ac;

    msg->reset(new (&ac) MessagePacket(data_size, num_handles));
    if (!ac.check())
        return ERR_NO_MEMORY;

    if (data_size > 0u) {
        (*msg)->data_.reset(new (&ac) uint8_t[data_size]);
        if (!ac.check())
            return ERR_NO_MEMORY;
    }

    if (num_handles > 0u) {
        (*msg)->handles_.reset(new (&ac) Handle*[num_handles]);
        if (!ac.check())
            return ERR_NO_MEMORY;
    }

    return NO_ERROR;
}

MessagePacket::~MessagePacket() {
    if (owns_handles_) {
        for (uint32_t i = 0; i < num_handles_; i++)
            DeleteHandle(handles_[i]);
    }
}

MessagePacket::MessagePacket(uint32_t data_size, uint32_t num_handles)
    : owns_handles_(false), data_size_(data_size), num_handles_(num_handles) {
}
