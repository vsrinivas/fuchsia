// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <trace-reader/reader_internal.h>

#include <inttypes.h>

#include <fbl/string_printf.h>

namespace trace {
namespace internal {

fbl::String BufferHeaderReader::Create(
    const void* header, size_t buffer_size,
    fbl::unique_ptr<BufferHeaderReader>* out_reader) {
    if (buffer_size < sizeof(trace_buffer_header)) {
        return "buffer too small for header";
    }
    auto hdr = reinterpret_cast<const trace_buffer_header*>(header);
    auto error = Validate(*hdr, buffer_size);
    if (error != "") {
        return error;
    }
    *out_reader = fbl::unique_ptr<BufferHeaderReader>(
        new BufferHeaderReader(hdr));
    return "";
}

BufferHeaderReader::BufferHeaderReader(const trace_buffer_header* header)
    : header_(header) {
}

fbl::String BufferHeaderReader::Validate(const trace_buffer_header& header,
                                         size_t buffer_size) {
    if (header.magic != TRACE_BUFFER_HEADER_MAGIC) {
        return fbl::StringPrintf("bad magic: 0x%" PRIx64, header.magic);
    }
    if (header.version != TRACE_BUFFER_HEADER_V0) {
        return fbl::StringPrintf("bad version: %u", header.version);
    }

    if (buffer_size & 7u) {
        return fbl::StringPrintf("buffer size not multiple of 64-bit words: 0x%" PRIx64,
                                 buffer_size);
    }

    switch (header.buffering_mode) {
    case TRACE_BUFFERING_MODE_ONESHOT:
    case TRACE_BUFFERING_MODE_CIRCULAR:
    case TRACE_BUFFERING_MODE_STREAMING:
        break;
    default:
        return fbl::StringPrintf("bad buffering mode: %u",
                                 header.buffering_mode);
    }

    if (header.total_size != buffer_size) {
        return fbl::StringPrintf("bad total buffer size: 0x%" PRIx64,
                                 header.total_size);
    }

    auto nondurable_buffer_size = header.nondurable_buffer_size;
    auto durable_buffer_size = header.durable_buffer_size;

    if ((nondurable_buffer_size & 7) != 0) {
        return fbl::StringPrintf("bad nondurable buffer size: 0x%" PRIx64,
                                 nondurable_buffer_size);
    }
    if ((durable_buffer_size & 7) != 0) {
        return fbl::StringPrintf("bad durable buffer size: 0x%" PRIx64,
                                 durable_buffer_size);
    }

    if (header.buffering_mode == TRACE_BUFFERING_MODE_ONESHOT) {
        if (nondurable_buffer_size != buffer_size - sizeof(trace_buffer_header)) {
            return fbl::StringPrintf("bad nondurable buffer size: 0x%" PRIx64,
                                     nondurable_buffer_size);
        }
        if (durable_buffer_size != 0) {
            return fbl::StringPrintf("bad durable buffer size: 0x%" PRIx64,
                                     durable_buffer_size);
        }
    } else {
        if (nondurable_buffer_size >= buffer_size / 2) {
            return fbl::StringPrintf("bad nondurable buffer size: 0x%" PRIx64,
                                     nondurable_buffer_size);
        }
        if (durable_buffer_size >= nondurable_buffer_size) {
            return fbl::StringPrintf("bad durable buffer size: 0x%" PRIx64,
                                     durable_buffer_size);
        }
        if ((sizeof(trace_buffer_header) + durable_buffer_size +
             2 * nondurable_buffer_size) != buffer_size) {
            return fbl::StringPrintf("buffer sizes don't add up:"
                                     " 0x%" PRIx64 ", 0x%" PRIx64,
                                     durable_buffer_size,
                                     nondurable_buffer_size);
        }
    }

    for (size_t i = 0; i < fbl::count_of(header.nondurable_data_end); ++i) {
        auto data_end = header.nondurable_data_end[i];
        if (data_end > nondurable_buffer_size ||
            (data_end & 7) != 0) {
            return fbl::StringPrintf("bad data end for buffer %zu: 0x%" PRIx64,
                                     i, data_end);
        }
    }

    auto durable_data_end = header.durable_data_end;
    if (durable_data_end > durable_buffer_size ||
        (durable_data_end & 7) != 0) {
        return fbl::StringPrintf("bad durable_data_end: 0x%" PRIx64,
                                 durable_data_end);
    }

    return "";
}

TraceBufferReader::TraceBufferReader(ChunkConsumer chunk_consumer,
                                     ErrorHandler error_handler)
    : chunk_consumer_(fbl::move(chunk_consumer)),
      error_handler_(fbl::move(error_handler)) {
}

bool TraceBufferReader::ReadChunks(const void* buffer, size_t buffer_size) {
    fbl::unique_ptr<BufferHeaderReader> header;
    auto error = BufferHeaderReader::Create(buffer, buffer_size, &header);
    if (error != "") {
        error_handler_(error);
        return false;
    }

    CallChunkConsumerIfNonEmpty(header->GetDurableBuffer(buffer),
                                header->durable_data_end());

    // There's only two buffers, thus the earlier one is not the current one.
    // It's important to process them in chronological order on the off
    // chance that the earlier buffer provides a stringref or threadref
    // referenced by the later buffer.
    int later_buffer = header->GetBufferNumber(header->wrapped_count());
    int earlier_buffer = 0;
    if (header->wrapped_count() > 0)
        earlier_buffer = header->GetBufferNumber(header->wrapped_count() - 1);

    if (earlier_buffer != later_buffer) {
        CallChunkConsumerIfNonEmpty(header->GetNondurableBuffer(buffer,
                                                                earlier_buffer),
                                    header->nondurable_data_end(earlier_buffer));
    }

    CallChunkConsumerIfNonEmpty(header->GetNondurableBuffer(buffer,
                                                            later_buffer),
                                header->nondurable_data_end(later_buffer));

    return true;
}

void TraceBufferReader::CallChunkConsumerIfNonEmpty(const void* ptr,
                                                    size_t size) {
    if (size != 0) {
        auto word_size = sizeof(uint64_t);
        ZX_DEBUG_ASSERT((reinterpret_cast<uintptr_t>(ptr) & (word_size - 1)) == 0);
        ZX_DEBUG_ASSERT((size & (word_size - 1)) == 0);
        Chunk chunk(reinterpret_cast<const uint64_t*>(ptr), size / word_size);
        chunk_consumer_(chunk);
    }
}

} // namespace internal
} // namespace trace
