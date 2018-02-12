// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debug-ipc/message_writer.h"

#include "fbl/type_support.h"

namespace debug_ipc {

namespace {

constexpr uint32_t kInitialSize = 32;

}  // namespace

MessageWriter::MessageWriter() : MessageWriter(kInitialSize) {}

MessageWriter::MessageWriter(uint32_t initial_size)
    : buffer_len_(initial_size > kInitialSize ? initial_size : kInitialSize),
      buffer_(new char[buffer_len_]) {}

MessageWriter::~MessageWriter() {}

void MessageWriter::WriteBytes(const void* data, uint32_t len) {
    if (write_offset_ + len > buffer_len_) {
        // Reallocate, make room for len bytes, and always at least double the size.
        uint32_t new_size = buffer_len_ * 2;
        if (write_offset_ + len > new_size)
            new_size = write_offset_ + len;
        shared::unique_ptr<char[]> new_buffer(new char[new_size]);

        if (write_offset_ > 0)
            memcpy(&new_buffer[0], &buffer_[0], write_offset_);
        buffer_ = fbl::move(new_buffer);
        buffer_len_ = new_size;
    }
    memcpy(&buffer_[write_offset_], data, len);
    write_offset_ += len;
}

void MessageWriter::WriteInt32(int32_t i) { WriteBytes(&i, sizeof(int32_t)); }
void MessageWriter::WriteUint32(uint32_t i) { WriteBytes(&i, sizeof(uint32_t)); }
void MessageWriter::WriteInt64(int64_t i) { WriteBytes(&i, sizeof(int64_t)); }
void MessageWriter::WriteUint64(uint64_t i) { WriteBytes(&i, sizeof(uint64_t)); }

void MessageWriter::WriteString(const shared::string& str) {
    // 32-bit size first, followed by bytes.
    uint32_t size = static_cast<uint32_t>(str.size());
    WriteUint32(size);
    if (!str.empty()) WriteBytes(&str[0], size);
}

void MessageWriter::WriteHeader(MsgHeader::Type type) {
    WriteUint32(0);  // Size to be filled in by GetDataAndWriteSize() later.
    WriteUint32(static_cast<uint32_t>(type));
}

const char* MessageWriter::GetDataAndWriteSize(uint32_t* size) {
    *size = static_cast<uint32_t>(write_offset_);
    memcpy(&buffer_[0], size, sizeof(uint32_t));
    return &buffer_[0];
}

}  // namespace debug_ipc
