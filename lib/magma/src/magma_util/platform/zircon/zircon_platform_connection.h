// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_ZIRCON_ZIRCON_PLATFORM_CONNECTION_H_
#define GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_ZIRCON_ZIRCON_PLATFORM_CONNECTION_H_

#include <stddef.h>
#include <stdint.h>

namespace magma {

constexpr size_t kReceiveBufferSize = 2048;

enum OpCode {
    ImportBuffer,
    ReleaseBuffer,
    ImportObject,
    ReleaseObject,
    CreateContext,
    DestroyContext,
    ExecuteCommandBuffer,
    GetError,
    MapBufferGpu,
    UnmapBufferGpu,
    CommitBuffer,
    ExecuteImmediateCommands,
};

struct ImportBufferOp {
    const OpCode opcode = ImportBuffer;
    static constexpr uint32_t kNumHandles = 1;
} __attribute__((packed));

struct ReleaseBufferOp {
    const OpCode opcode = ReleaseBuffer;
    static constexpr uint32_t kNumHandles = 0;
    uint64_t buffer_id;
} __attribute__((packed));

struct ImportObjectOp {
    const OpCode opcode = ImportObject;
    uint32_t object_type;
    static constexpr uint32_t kNumHandles = 1;
} __attribute__((packed));

struct ReleaseObjectOp {
    const OpCode opcode = ReleaseObject;
    static constexpr uint32_t kNumHandles = 0;
    uint64_t object_id;
    uint32_t object_type;
} __attribute__((packed));

struct CreateContextOp {
    const OpCode opcode = CreateContext;
    static constexpr uint32_t kNumHandles = 0;
    uint32_t context_id;
} __attribute__((packed));

struct DestroyContextOp {
    const OpCode opcode = DestroyContext;
    static constexpr uint32_t kNumHandles = 0;
    uint32_t context_id;
} __attribute__((packed));

struct ExecuteCommandBufferOp {
    const OpCode opcode = ExecuteCommandBuffer;
    static constexpr uint32_t kNumHandles = 1;
    uint32_t context_id;
} __attribute__((packed));

struct ExecuteImmediateCommandsOp {
    const OpCode opcode = ExecuteImmediateCommands;
    static constexpr uint32_t kNumHandles = 0;
    uint32_t context_id;
    uint32_t semaphore_count;
    uint32_t commands_size;
    uint64_t semaphores[];
    // Command data follows the last semaphore.

    uint8_t* command_data() { return reinterpret_cast<uint8_t*>(&semaphores[semaphore_count]); }

    static uint32_t size(uint32_t semaphore_count, uint32_t commands_size)
    {
        return sizeof(ExecuteImmediateCommandsOp) + commands_size +
               sizeof(uint64_t) * semaphore_count;
    }
} __attribute__((packed));

struct GetErrorOp {
    const OpCode opcode = GetError;
    static constexpr uint32_t kNumHandles = 0;
} __attribute__((packed));

struct MapBufferGpuOp {
    const OpCode opcode = MapBufferGpu;
    static constexpr uint32_t kNumHandles = 0;
    uint64_t buffer_id;
    uint64_t gpu_va;
    uint64_t page_offset;
    uint64_t page_count;
    uint64_t flags;
} __attribute__((packed));

struct UnmapBufferGpuOp {
    const OpCode opcode = UnmapBufferGpu;
    static constexpr uint32_t kNumHandles = 0;
    uint64_t buffer_id;
    uint64_t gpu_va;
} __attribute__((packed));

struct CommitBufferOp {
    const OpCode opcode = CommitBuffer;
    static constexpr uint32_t kNumHandles = 0;
    uint64_t buffer_id;
    uint64_t page_offset;
    uint64_t page_count;
} __attribute__((packed));

} // namespace magma

#endif  // GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_ZIRCON_ZIRCON_PLATFORM_CONNECTION_H_
