// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <cstdlib>
#include <utility>
#include <memory>

#include <fbl/string_buffer.h>
#include <fs/trace.h>
#include <zircon/device/vfs.h>

// fs-internal header defining utility functions for logging flags and paths.

namespace fs {

// Marker type for pretty-printing flags
struct ZxFlags {
public:
    explicit ZxFlags(uint32_t flags) : value(flags) {}
    uint32_t value;
};

struct Path {
    Path(const char* path, size_t size) : str(path), size(size) {}
    const char* str;
    size_t size;
};

namespace debug_internal {

constexpr const char* FlagToString(uint32_t flag) {
    switch (flag) {
        case ZX_FS_RIGHT_ADMIN:
            return "RIGHT_ADMIN";
        case ZX_FS_RIGHT_READABLE:
            return "RIGHT_READABLE";
        case ZX_FS_RIGHT_WRITABLE:
            return "RIGHT_WRITABLE";
        case ZX_FS_RIGHTS:
            return "RIGHTS";
        case ZX_FS_FLAG_CREATE:
            return "FLAG_CREATE";
        case ZX_FS_FLAG_EXCLUSIVE:
            return "FLAG_EXCLUSIVE";
        case ZX_FS_FLAG_TRUNCATE:
            return "FLAG_TRUNCATE";
        case ZX_FS_FLAG_DIRECTORY:
            return "FLAG_DIRECTORY";
        case ZX_FS_FLAG_APPEND:
            return "FLAG_APPEND";
        case ZX_FS_FLAG_NOREMOTE:
            return "FLAG_NOREMOTE";
        case ZX_FS_FLAG_VNODE_REF_ONLY:
            return "FLAG_VNODE_REF_ONLY";
        case ZX_FS_FLAG_DESCRIBE:
            return "FLAG_DESCRIBE";
        case ZX_FS_FLAG_POSIX:
            return "FLAG_POSIX";
        case ZX_FS_FLAG_NOT_DIRECTORY:
            return "FLAG_NOT_DIRECTORY";
        case ZX_FS_FLAG_CLONE_SAME_RIGHTS:
            return "FLAG_CLONE_SAME_RIGHTS";
        default:
            return "(Unknown flag)";
    }
}

template <size_t N>
void PrintIntoStringBuffer(fbl::StringBuffer<N>* sb, ZxFlags flags) {
    bool first = true;
    uint32_t bit = 1;
    for (int i = 0; i < 32; i++) {
        const uint32_t flag = flags.value & bit;
        if (flag) {
            const char* desc = FlagToString(flag);
            if (!first) {
                sb->Append(" | ");
            }
            first = false;
            sb->Append(desc);
        }
        bit = bit << 1U;
    }
}

template <size_t N>
void PrintIntoStringBuffer(fbl::StringBuffer<N>* sb, const char* str) {
    sb->Append(str);
}

template <size_t N>
void PrintIntoStringBuffer(fbl::StringBuffer<N>* sb, Path path) {
    sb->Append(path.str, path.size);
}

template <size_t N>
void PrintEach(fbl::StringBuffer<N>* sb) {
}

template <size_t N, typename T, typename... Args>
void PrintEach(fbl::StringBuffer<N>* sb, T val, Args... args) {
    PrintIntoStringBuffer(sb, val);
    PrintEach(sb, args...);
}

template <typename... Args>
void ConnectionTraceDebug(Args... args) {
    constexpr size_t kMaxSize = 2000;
    auto str = std::make_unique<fbl::StringBuffer<kMaxSize>>();
    PrintEach(str.get(), args...);
    FS_TRACE_DEBUG("%s\n", str->c_str());
}

}  // namespace debug_internal

}  // namespace fs

#if FS_TRACE_DEBUG_ENABLED
#define FS_PRETTY_TRACE_DEBUG(args...) fs::debug_internal::ConnectionTraceDebug(args)
#else
// Explicitly expand FS_PRETTY_TRACE_DEBUG into nothing when not debugging, to ensure zero overhead.
#define FS_PRETTY_TRACE_DEBUG(args...)
#endif

