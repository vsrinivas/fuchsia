// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debug-ipc/message_reader.h"

#include <string.h>

#include "debug-ipc/protocol.h"

namespace debug_ipc {

MessageReader::MessageReader(const char* message, uint32_t len)
    : message_(message), message_len_(len) {}
MessageReader::~MessageReader() {}

bool MessageReader::ReadBytes(uint32_t len, void* output) {
    if (message_len_ - offset_ < len) return SetError();
    memcpy(output, &message_[offset_], len);
    offset_ += len;
    return true;
}

bool MessageReader::ReadInt32(int32_t* output) { return ReadBytes(sizeof(int32_t), output); }
bool MessageReader::ReadUint32(uint32_t* output) { return ReadBytes(sizeof(uint32_t), output); }
bool MessageReader::ReadInt64(int64_t* output) { return ReadBytes(sizeof(int64_t), output); }
bool MessageReader::ReadUint64(uint64_t* output) { return ReadBytes(sizeof(uint64_t), output); }

bool MessageReader::ReadString(shared::string* output) {
    // Size header.
    uint32_t str_len;
    if (!ReadUint32(&str_len)) return SetError();
    if (str_len > remaining()) return SetError();

    // String bytes.
    *output = shared::string(str_len, '\0');
    if (str_len == 0) return true;
    if (!ReadBytes(str_len, const_cast<char*>(&(*output)[0]))) {
        *output = shared::string();
        return SetError();
    }
    return true;
}

bool MessageReader::ReadHeader(MsgHeader* output) {
    if (!ReadUint32(&output->size)) return false;

    uint32_t type;
    if (!ReadUint32(&type)) return false;
    if (type >= static_cast<uint32_t>(MsgHeader::Type::kNumMessages)) return false;
    output->type = static_cast<MsgHeader::Type>(type);
    return true;
}

bool MessageReader::SetError() {
    has_error_ = true;
    return false;
}

}  // namespace debug_ipc
