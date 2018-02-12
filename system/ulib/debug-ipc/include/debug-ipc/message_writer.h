// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "debug-ipc/shared.h"
#include "debug-ipc/protocol.h"

namespace debug_ipc {

struct MsgHeader;

// Provides a simple means to append to a dynamic buffer different types of data.
//
// The first 4 bytes of each message is the message size. It's assumed that these bytes will be
// explicitly written to. Normally a message will start with a struct which contains space for this
// explicitly.
class MessageWriter {
  public:
    MessageWriter();
    // The argument is a hint for the initial size of the message.
    explicit MessageWriter(uint32_t initial_size);
    ~MessageWriter();

    void WriteBytes(const void* data, uint32_t len);
    void WriteInt32(int32_t i);
    void WriteUint32(uint32_t i);
    void WriteInt64(int64_t i);
    void WriteUint64(uint64_t i);
    void WriteString(const shared::string& str);

    void WriteHeader(MsgHeader::Type type);

    // Writes the size of the current buffer to the first 8 bytes, and returns a pointer to the
    // beginning of the buffer. The size will also be written to the given out param.
    const char* GetDataAndWriteSize(uint32_t* size);

  private:
    // This buffer will be reallocated as needed.
    uint32_t buffer_len_;
    shared::unique_ptr<char[]> buffer_;

    uint32_t write_offset_ = 0;
};

}  // namespace debug_ipc
