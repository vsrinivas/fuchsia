// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/message_packet.h>

#include <err.h>

#include <magenta/handle_reaper.h>
#include <magenta/magenta.h>
#include <mxcpp/new.h>

constexpr uint32_t kMaxMessageSize = 65536u;
constexpr uint32_t kMaxMessageHandles = 1024u;

// static
mx_status_t MessagePacket::Create(uint32_t data_size, uint32_t num_handles,
                                  mxtl::unique_ptr<MessagePacket>* msg) {
    if (data_size > kMaxMessageSize)
        return ERR_OUT_OF_RANGE;
    if (num_handles > kMaxMessageHandles)
        return ERR_OUT_OF_RANGE;

    // Allocate space for the MessagePacket object followed by num_handles
    // Handle*s followed by data_size bytes.
    char* ptr = static_cast<char*>(malloc(sizeof(MessagePacket) +
                                          num_handles * sizeof(Handle*) +
                                          data_size));
    if (ptr == nullptr)
        return ERR_NO_MEMORY;

    // The storage space for the Handle*s and bytes is not initialized
    // because the only creators of MessagePackets (sys_channel_write and _call)
    // fill these arrays immediately after creation of the object.
    msg->reset(new (ptr) MessagePacket(data_size, num_handles,
                                       reinterpret_cast<Handle**>(ptr + sizeof(MessagePacket))));
    return NO_ERROR;
}

MessagePacket::~MessagePacket() {
    if (owns_handles_) {
        // Delete handles out-of-band to avoid the worst case recursive
        // destruction behavior.
        ReapHandles(handles_, num_handles_);
    }
}

MessagePacket::MessagePacket(uint32_t data_size, uint32_t num_handles, Handle** handles)
    : owns_handles_(false), data_size_(data_size), num_handles_(num_handles), handles_(handles) {
}
