// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "debug-ipc/records.h"

namespace debug_ipc {

constexpr uint32_t kProtocolVersion = 1;

#pragma pack(push, 8)

// A message consists of a MsgHeader followed by a serialized version of whatever struct is
// associated with that message type. Use the MessageWriter class to build this up, which will
// reserve room for the header and allows the structs to be appended, possibly dynamically.
struct MsgHeader {
    enum class Type : uint32_t {
        kNone = 0,
        kHello,
        kProcessTree,
        kThreads,
        kReadMemory,

        kNumMessages
    };

    MsgHeader() = default;
    explicit MsgHeader(Type t) : type(t) {}

    uint32_t size = 0;  // Size includes this header.
    Type type = Type::kNone;

    // The transaction ID is assigned by the sender of a request, and is echoed in the reply so the
    // transaction can be easily correlated.
    uint32_t transaction_id = 0;

    static constexpr uint32_t kSerializedHeaderSize = sizeof(uint32_t) * 3;
};

struct HelloRequest {};
struct HelloReply {
    uint32_t version;
};

struct ProcessTreeRequest {};
struct ProcessTreeReply {
    ProcessTreeRecord root;
};

struct ThreadsRequest {
    uint64_t process_koid;
};
struct ThreadsReply {
    // If there is no such process, the threads array will be empty.
    shared::vector<ThreadRecord> threads;
};

struct ReadMemoryRequest {
    uint64_t process_koid;
    uint64_t address;
    uint32_t size;
};
struct ReadMemoryReply {
    shared::vector<MemoryBlock> blocks;
};

#pragma pack(pop)

}  // namespace debug_ipc
