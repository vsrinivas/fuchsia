// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lz4/lz4frame.h>
#include <stdio.h>
#include <unistd.h>

#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/macros.h>
#include <fbl/unique_ptr.h>
#include <fs/trace.h>
#include <zircon/types.h>

#include <blobfs/lz4.h>

namespace blobfs {

Compressor::Compressor() : buf_(nullptr) {}

Compressor::~Compressor() {
    Reset();
}

void Compressor::Reset() {
    if (Compressing()) {
        LZ4F_freeCompressionContext(ctx_);
    }
    buf_ = nullptr;
}

zx_status_t Compressor::Initialize(void* buf, size_t buf_max) {
    ZX_DEBUG_ASSERT(!Compressing());
    LZ4F_errorCode_t errc = LZ4F_createCompressionContext(&ctx_, LZ4F_VERSION);
    if (LZ4F_isError(errc)) {
        return ZX_ERR_NO_MEMORY;
    }

    buf_ = buf;
    buf_max_ = buf_max;
    buf_used_ = 0;

    size_t r = LZ4F_compressBegin(ctx_, Buffer(), buf_remaining(), nullptr);
    if (LZ4F_isError(r)) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }
    buf_used_ += r;

    return ZX_OK;
}

zx_status_t Compressor::Update(const void* data, size_t length) {
    size_t r = LZ4F_compressUpdate(ctx_, Buffer(), buf_remaining(), data, length, nullptr);
    if (LZ4F_isError(r)) {
        return ZX_ERR_IO_DATA_INTEGRITY;
    }
    buf_used_ += r;
    return ZX_OK;
}

zx_status_t Compressor::End() {
    size_t r = LZ4F_compressEnd(ctx_, Buffer(), buf_remaining(), nullptr);
    if (LZ4F_isError(r)) {
        return ZX_ERR_IO_DATA_INTEGRITY;
    }
    buf_used_ += r;
    return ZX_OK;
}

size_t Compressor::Size() const {
    ZX_DEBUG_ASSERT(Compressing());
    return buf_used_;
}

zx_status_t Decompressor::Decompress(void* target_buf_, size_t* target_size,
                                     const void* src_buf_, size_t* src_size) {
    TRACE_DURATION("blobfs", "Decompressor::Decompress", "target_size", *target_size,
                   "src_size", *src_size);
    uint8_t* target_buf = reinterpret_cast<uint8_t*>(target_buf_);
    const uint8_t* src_buf = reinterpret_cast<const uint8_t*>(src_buf_);

    LZ4F_decompressionContext_t ctx;
    LZ4F_errorCode_t errc = LZ4F_createDecompressionContext(&ctx, LZ4F_VERSION);
    if (LZ4F_isError(errc)) {
        return ZX_ERR_NO_MEMORY;
    }

    auto cleanup = fbl::MakeAutoCall([&ctx]() {
        LZ4F_freeDecompressionContext(ctx);
    });
    size_t target_drained = 0;
    size_t src_drained = 0;

    // Decompress the first four bytes of the source without consuming the
    // destination buffer to determine the size of the frame header.
    size_t dst_sz_next = 0;
    size_t src_sz_next = 4;

    while (true) {
        uint8_t* target = target_buf + target_drained;
        const uint8_t* src = src_buf + src_drained;

        size_t r = LZ4F_decompress(ctx, target, &dst_sz_next, src, &src_sz_next, nullptr);
        if (LZ4F_isError(r)) {
            return ZX_ERR_IO_DATA_INTEGRITY;
        }

        // After the call to decompress, these are the sizes which were used.
        target_drained += dst_sz_next;
        src_drained += src_sz_next;

        if (r == 0) {
            break;
        }

        dst_sz_next = *target_size - target_drained;
        src_sz_next = r;

    }

    *target_size = target_drained;
    *src_size = src_drained;
    return ZX_OK;
}

} // namespace blobfs
