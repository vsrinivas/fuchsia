// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lz4/lz4frame.h>

namespace blobfs {

// A Compressor is used to compress a blob transparently before it is written
// back to disk.
class Compressor {
public:
    Compressor();

    ~Compressor();

    // Identifies if compression is underway.
    bool Compressing() const {
        return buf_ != nullptr;
    }

    // Resets the compression process.
    void Reset();

    // Returns the compressed size of the blob so far.
    size_t Size() const;

    // Initializes the compression object with a provided
    // buffer of a specified size.
    //
    // Although Compressor uses this buffer, it does not own the buffer,
    // assuming that a parent object is responsible for the lifetime.
    zx_status_t Initialize(void* buf, size_t buf_max);

    // Returns the maximum possible size a buffer would need to be
    // in order to compress a blob of size |blob_size|.
    //
    // Typically used in conjunction with |Initialize()|.
    size_t BufferMax(size_t blob_size) const {
        return LZ4F_compressBound(blob_size, nullptr);
    }

    // Continues the compression after initialization.
    zx_status_t Update(const void* data, size_t length);

    // Finishes the compression process. Must be called
    // before compression is considered complete.
    zx_status_t End();

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Compressor);

    void* Buffer() const {
        return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(buf_) + buf_used_);
    }

    size_t buf_remaining() const { return buf_max_ - buf_used_; }

    LZ4F_compressionContext_t ctx_;
    void* buf_;
    size_t buf_max_;
    size_t buf_used_;
};

// A Decompressor is used to decompress a blob transparently before it is
// read back from disk.
class Decompressor {
public:
    // Decompress the source buffer into the target buffer,
    // until either the source is drained or the target is
    // filled (or both).
    static zx_status_t Decompress(void* target_buf, size_t* target_size,
                                  const void* src_buf, size_t* src_size);
};

} // namespace blobfs
