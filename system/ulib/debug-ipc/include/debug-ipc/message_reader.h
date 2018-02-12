// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include "debug-ipc/shared.h"

namespace debug_ipc {

struct MsgHeader;

class MessageReader {
  public:
    MessageReader(const char* message, uint32_t len);
    ~MessageReader();

    bool has_error() const { return has_error_; }

    // Returns the number of bytes available still to read.
    uint32_t remaining() const { return message_len_ - offset_; }

    // These functions return true on success.
    bool ReadBytes(uint32_t len, void* output);
    bool ReadInt32(int32_t* output);
    bool ReadUint32(uint32_t* output);
    bool ReadInt64(int64_t* output);
    bool ReadUint64(uint64_t* output);
    bool ReadString(shared::string* output);

    bool ReadHeader(MsgHeader* output);

  private:
    // Sets the error flag and returns false. This is designed so that error handling code
    // need only call "return SetError();".
    bool SetError();

    const char* message_;  // Non-owning.
    const uint32_t message_len_;

    uint32_t offset_ = 0;  // Current read offset.

    bool has_error_ = false;
};

}  // namespace debug_ipc
