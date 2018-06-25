// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <lz4/lz4frame.h>
#include <fbl/auto_call.h>
#include <fbl/unique_fd.h>
#include "fvm/fvm-sparse.h"

#ifdef __Fuchsia__
#include <zircon/syscalls.h>
#endif

#define LZ4_MAX_BLOCK_SIZE 65536

namespace fvm {

class SparseReader {
public:
    static zx_status_t Create(fbl::unique_fd fd, fbl::unique_ptr<SparseReader>* out);
    ~SparseReader();

    fvm::sparse_image_t* Image();
    fvm::partition_descriptor_t* Partitions();

    // Read requested data from sparse file into buffer
    zx_status_t ReadData(uint8_t* data, size_t length, size_t *actual);
    // Write decompressed data into new file
    zx_status_t WriteDecompressed(fbl::unique_fd outfd);
private:
    typedef struct buffer {
        // Write |length| bytes from |indata| into buffer.
        bool is_empty() {
            return offset == 0 && size == 0;
        }

        void write(uint8_t* indata, size_t length) {
            ZX_ASSERT(length <= max_size);
            // We should have read all previous data from buffer before writing more.
            ZX_ASSERT(is_empty());

            if (length > 0) {
                memcpy(data.get(), indata, length);
                size = length;
            }
        }

        // Read up to |length| bytes from buffer into |outdata|, returning |actual| bytes copied.
        void read(uint8_t* outdata, size_t length, size_t* actual) {
            size_t cp_sz = fbl::min(length, size);

            if (cp_sz > 0) {
                memcpy(outdata, data.get() + offset, cp_sz);
                offset += cp_sz;
            }

            size -= cp_sz;

            if (size == 0) {
                offset = 0;
            }

            *actual = cp_sz;
        }

        // Data buffer
        fbl::unique_ptr<uint8_t[]> data;
        // Actual size of data contained within buffer
        size_t size;
        // Offset into buffer where valid data begins
        size_t offset;
        // Maximum size allocated for buffer
        size_t max_size;
    } buffer_t;

    SparseReader(fbl::unique_fd fd);
    // Read in header data, prepare buffers and decompression context if necessary
    zx_status_t ReadMetadata();
    // Initialize buffer with a given |size|
    static zx_status_t InitializeBuffer(size_t size, buffer_t* out_buf);
    // Read |length| bytes of raw data from file directly into |data|. Return |actual| bytes read.
    zx_status_t ReadRaw(uint8_t* data, size_t length, size_t* actual);

    void PrintStats() const;

    // True if sparse file is compressed
    bool compressed_;

    fbl::unique_fd fd_;
    fbl::unique_ptr<uint8_t[]> metadata_;
    LZ4F_decompressionContext_t dctx_;
    // A hint of the size of the next compressed frame to be decompressed.
    // May be an overestimate, but will not be an underestimate (0 indicates no more data left to
    // decompress).
    size_t to_read_;

    // Buffer for compressed data read directly from file
    buffer_t in_buf_;
    // Buffer for decompressed data
    buffer_t out_buf_;

#ifdef __Fuchsia__
    // Total time spent reading/decompressing data
    zx_time_t total_time_ = 0;
    // Total time spent reading data from fd
    zx_time_t read_time_ = 0;
#endif
};

// Read from compressed |infile|, decompress, and write to |outfile|.
zx_status_t decompress_sparse(const char* infile, const char* outfile);
} // namespace fvm
