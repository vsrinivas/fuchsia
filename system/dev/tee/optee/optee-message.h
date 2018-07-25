// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <inttypes.h>

#include <fbl/array.h>
#include <fbl/unique_ptr.h>
#include <zircon/assert.h>

#include "shared-memory.h"
#include "util.h"

namespace optee {

// OP-TEE Messages
//
// The majority of data exchange with OP-TEE occurs via OP-TEE messages. These are used in
// conjunction with the OP-TEE SMC Call with Arg function. When that SMC function is invoked,
// OP-TEE will expect a physical pointer to an OP-TEE message to be passed in arguments a1 and a2.
//
// Each message is made up of a header and a variable number of parameters. The relevant fields of
// a message can depend on the command and the context, so these helper classes aim to reduce the
// possibilities of invariant access. For example, in some instances, a field might be an input and
// in others, it might be an output.

struct MessageHeader {
    uint32_t command;
    uint32_t app_function;
    uint32_t session_id;
    uint32_t cancel_id;

    uint32_t unused;
    uint32_t return_code;
    uint32_t return_origin;
    uint32_t num_params;
};

struct MessageParam {
    enum AttributeType : uint64_t {
        kAttributeTypeNone = 0x0,
        kAttributeTypeValueInput = 0x1,
        kAttributeTypeValueOutput = 0x2,
        kAttributeTypeValueInOut = 0x3,
        kAttributeTypeRegMemInput = 0x5,
        kAttributeTypeRegMemOutput = 0x6,
        kAttributeTypeRegMemInOut = 0x7,
        kAttributeTypeTempMemInput = 0x9,
        kAttributeTypeTempMemOutput = 0xa,
        kAttributeTypeTempMemInOut = 0xb,

        kAttributeTypeMeta = 0x100,
        kAttributeTypeFragment = 0x200,
    };

    struct TemporaryMemory {
        uint64_t buffer;
        uint64_t size;
        uint64_t shared_memory_reference;
    };

    struct RegisteredMemory {
        uint64_t offset;
        uint64_t size;
        uint64_t shared_memory_reference;
    };

    struct Value {
        uint64_t a;
        uint64_t b;
        uint64_t c;
    };

    uint64_t attribute;
    union {
        TemporaryMemory temporary_memory;
        RegisteredMemory registered_memory;
        Value value;
    } payload;
};

// MessageParamList
//
// MessageParamList is a non-owning view of the parameters in a Message. It is only valid within
// the lifetime of the Message.
class MessageParamList {
public:
    constexpr MessageParamList()
        : params_(nullptr), count_(0U) {}

    MessageParamList(MessageParam* params, size_t count)
        : params_(params), count_(count) {}

    size_t size() const { return count_; }
    MessageParam* get() const { return params_; }

    MessageParam& operator[](size_t i) const {
        ZX_DEBUG_ASSERT(i < count_);
        return params_[i];
    }

    MessageParam* begin() const {
        return params_;
    }
    MessageParam* end() const {
        return &params_[count_];
    }

private:
    MessageParam* params_;
    size_t count_;
};

class Message {
public:
    enum Command : uint32_t {
        kOpenSession = 0,
        kInvokeCommand = 1,
        kCloseSession = 2,
        kCancel = 3,
        kRegisterSharedMemory = 4,
        kUnregisterSharedMemory = 5,
    };

    zx_paddr_t paddr() const { return memory_->paddr(); }

protected:
    static constexpr size_t CalculateSize(size_t num_params) {
        return sizeof(MessageHeader) + (sizeof(MessageParam) * num_params);
    }

    MessageHeader* header() const {
        return reinterpret_cast<MessageHeader*>(memory_->vaddr());
    }

    // TODO(rjascani): Change this to return a reference to make ownership rules clearer
    MessageParamList params() const {
        return MessageParamList(reinterpret_cast<MessageParam*>(header() + 1),
                                header()->num_params);
    }

    fbl::unique_ptr<SharedMemory> memory_;
};

// OpenSessionMessage
//
// This OP-TEE message is used to start a session between a client app and trusted app.
class OpenSessionMessage : public Message {
public:
    static OpenSessionMessage Create(SharedMemoryManager::DriverMemoryPool* pool,
                                     const UuidView& trusted_app,
                                     const UuidView& client_app,
                                     uint32_t client_login,
                                     uint32_t cancel_id,
                                     const fbl::Array<MessageParam>& params);

    // Outputs
    uint32_t session_id() const { return header()->session_id; }
    uint32_t return_code() const { return header()->return_code; }
    uint32_t return_origin() const { return header()->return_origin; }
    using Message::params;
};

} // namespace optee
