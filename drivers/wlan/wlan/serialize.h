// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "interface.h"

namespace wlan {

template <typename T>
mx_status_t DeserializeServiceMsg(const Packet& packet, Method m, ::fidl::StructPtr<T>* out) {
    if (out == nullptr) return MX_ERR_INVALID_ARGS;

    const uint8_t* p = packet.data();
    auto h = FromBytes<Header>(p, packet.len());
    if (static_cast<Method>(h->ordinal) != m) return MX_ERR_IO;

    *out = T::New();
    auto reqptr = reinterpret_cast<const void*>(h->payload);
    if (!(*out)->Deserialize(const_cast<void*>(reqptr), packet.len() - h->len)) {
        return MX_ERR_IO;
    }
    return MX_OK;
}

template <typename T>
mx_status_t SerializeServiceMsg(Packet* packet, Method m, const ::fidl::StructPtr<T>& msg) {
    size_t buf_len = sizeof(Header) + msg->GetSerializedSize();
    auto header = FromBytes<Header>(packet->mut_data(), buf_len);
    if (header == nullptr) {
        return MX_ERR_BUFFER_TOO_SMALL;
    }
    header->len = sizeof(Header);
    header->txn_id = 1;  // TODO(tkilbourn): txn ids
    header->flags = 0;
    header->ordinal = static_cast<uint32_t>(m);
    if (!msg->Serialize(header->payload, buf_len - sizeof(Header))) {
        return MX_ERR_IO;
    }
    return MX_OK;
}

}  // namespace wlan
