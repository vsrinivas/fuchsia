// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fvm/fvm-lz4.h"

namespace fvm {
zx_status_t SparseReader::Create(fbl::unique_fd fd, fbl::unique_ptr<SparseReader>* out) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<SparseReader> reader(new (&ac) SparseReader(fbl::move(fd)));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status;
    if ((status = reader->ReadMetadata()) != ZX_OK) {
        return status;
    }

    *out = fbl::move(reader);
    return ZX_OK;
}

SparseReader::SparseReader(fbl::unique_fd fd) : compressed_(false), fd_(fbl::move(fd)) {}

zx_status_t SparseReader::ReadMetadata() {
    // Read sparse image header.
    fvm::sparse_image_t image;
    if (read(fd_.get(), &image, sizeof(fvm::sparse_image_t)) != sizeof(fvm::sparse_image_t)) {
        fprintf(stderr, "failed to read the sparse header\n");
        return ZX_ERR_IO;
    }

    // Verify the header.
    if (image.magic != fvm::kSparseFormatMagic) {
        fprintf(stderr, "SparseReader: Bad magic\n");
        return ZX_ERR_BAD_STATE;
    } else if (image.version != fvm::kSparseFormatVersion) {
        fprintf(stderr, "SparseReader: Unexpected sparse file version\n");
        return ZX_ERR_BAD_STATE;
    }

    fbl::AllocChecker ac;
    metadata_.reset(new (&ac) uint8_t[image.header_length]);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    memcpy(metadata_.get(), &image, sizeof(image));

    // Read remainder of metadata.
    size_t off = sizeof(image);
    while (off < image.header_length) {
        ssize_t r = read(fd_.get(), &metadata_[off], image.header_length - off);
        if (r < 0) {
            fprintf(stderr, "SparseReader: Failed to read metadata\n");
            return ZX_ERR_IO;
        }
        off += r;
    }

    // If image is compressed, additional setup is required
    if (image.flags & fvm::kSparseFlagLz4) {
        printf("Found compressed file\n");
        compressed_ = true;
        // Initialize decompression context
        LZ4F_errorCode_t errc = LZ4F_createDecompressionContext(&dctx_, LZ4F_VERSION);
        if (LZ4F_isError(errc)) {
            fprintf(stderr, "SparseReader: could not initialize decompression: %s\n",
                    LZ4F_getErrorName(errc));
            return ZX_ERR_INTERNAL;
        }

        size_t src_sz = 4;
        size_t dst_sz = 0;
        fbl::unique_ptr<uint8_t[]> inbufptr(new (&ac) uint8_t[src_sz]);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }

        uint8_t* inbuf = inbufptr.get();

        // Read first 4 bytes to let LZ4 tell us how much it expects in the first pass.
        ssize_t nr = read(fd_.get(), inbuf, src_sz);
        if (nr < static_cast<ssize_t>(src_sz)) {
            fprintf(stderr, "SparseReader: could not read from input\n");
            return ZX_ERR_IO;
        }

        // Run decompress once to find out how much data we should read for the next decompress run
        // Since we are not yet decompressing any actual data, the dst_buffer is null
        to_read_ = LZ4F_decompress(dctx_, nullptr, &dst_sz, inbuf, &src_sz, NULL);
        if (LZ4F_isError(to_read_)) {
            fprintf(stderr, "SparseReader: could not decompress header: %s\n",
                    LZ4F_getErrorName(to_read_));
            return ZX_ERR_INTERNAL;
        }

        if (to_read_ > LZ4_MAX_BLOCK_SIZE) {
            to_read_ = LZ4_MAX_BLOCK_SIZE;
        }

        // Initialize data buffers
        zx_status_t status;
        if ((status = InitializeBuffer(LZ4_MAX_BLOCK_SIZE, &out_buf_)) != ZX_OK) {
            return status;
        } else if ((status = InitializeBuffer(LZ4_MAX_BLOCK_SIZE, &in_buf_)) != ZX_OK) {
            return status;
        }
    }

    return ZX_OK;
}

zx_status_t SparseReader::InitializeBuffer(size_t size, buffer_t* out_buf) {
    if (size < LZ4_MAX_BLOCK_SIZE) {
        fprintf(stderr, "Buffer size must be >= %d\n", LZ4_MAX_BLOCK_SIZE);
        return ZX_ERR_INVALID_ARGS;
    }

    out_buf->max_size = size;
    out_buf->size = 0;
    out_buf->offset = 0;
    fbl::AllocChecker ac;
    out_buf->data.reset(new (&ac) uint8_t[size]);

    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    return ZX_OK;
}

SparseReader::~SparseReader() {
    PrintStats();

    if (compressed_) {
        LZ4F_freeDecompressionContext(dctx_);
    }
}

fvm::sparse_image_t* SparseReader::Image() {
    return reinterpret_cast<fvm::sparse_image_t*>(metadata_.get());
}

fvm::partition_descriptor_t* SparseReader::Partitions() {
    return reinterpret_cast<fvm::partition_descriptor_t*>(
                reinterpret_cast<uintptr_t>(metadata_.get()) +
                sizeof(fvm::sparse_image_t));
}

zx_status_t SparseReader::ReadData(uint8_t* data, size_t length, size_t* actual) {
#ifdef __Fuchsia__
    zx_time_t start = zx_ticks_get();
#endif
    size_t total_size = 0;
    if (compressed_) {
        if (out_buf_.is_empty() && to_read_ == 0) {
            // There is no more to read
            return ZX_ERR_OUT_OF_RANGE;
        }

        // Read previously decompressed data from buffer if possible
        out_buf_.read(data, length, &total_size);

        // If we still have data to read, start decompression (reading more from fd as needed)
        while (total_size < length && to_read_ > 0) {
            // Make sure data to read does not exceed max, and both buffers are empty
            ZX_ASSERT(out_buf_.is_empty());
            ZX_ASSERT(in_buf_.is_empty());
            ZX_ASSERT(to_read_ <= in_buf_.max_size);

            // Read specified amount from fd
            zx_status_t status;
            if ((status = ReadRaw(in_buf_.data.get(), to_read_, &in_buf_.size)) != ZX_OK) {
                return status;
            }

            size_t src_sz = in_buf_.size;
            size_t next = 0;

            // Decompress all compressed data
            while (in_buf_.offset < to_read_) {
                size_t dst_sz = out_buf_.max_size - out_buf_.size;
                next = LZ4F_decompress(dctx_, out_buf_.data.get() + out_buf_.size, &dst_sz,
                                       in_buf_.data.get() + in_buf_.offset, &src_sz, NULL);
                if (LZ4F_isError(next)) {
                    fprintf(stderr, "could not decompress input: %s\n", LZ4F_getErrorName(next));
                    return -1;
                }

                out_buf_.size += dst_sz;
                in_buf_.offset += src_sz;
                in_buf_.size -= src_sz;
                src_sz = to_read_ - in_buf_.offset;
            }

            // Make sure we have read all data from in_buf_
            ZX_ASSERT(in_buf_.size == 0);
            in_buf_.offset = 0;

            // Copy newly decompressed data from outbuf
            size_t cp = fbl::min(length - total_size, static_cast<size_t>(out_buf_.size));
            out_buf_.read(data + total_size, cp, &cp);
            total_size += cp;
            to_read_ = next;

            if (to_read_ > LZ4_MAX_BLOCK_SIZE) {
                to_read_ = LZ4_MAX_BLOCK_SIZE;
            }
        }
    } else {
        zx_status_t status = ReadRaw(data, length, &total_size);

        if (status != ZX_OK) {
            return status;
        }
    }

#ifdef __Fuchsia__
    total_time_ += zx_ticks_get() - start;
#endif
    *actual = total_size;
    return ZX_OK;
}

zx_status_t SparseReader::ReadRaw(uint8_t* data, size_t length, size_t* actual) {
#ifdef __Fuchsia__
    zx_time_t start = zx_ticks_get();
#endif
    ssize_t r;
    size_t total_size = 0;
    size_t bytes_left = length;
    while ((r = read(fd_.get(), data + total_size, bytes_left)) > 0) {
        total_size += r;
        bytes_left -= r;
        if (bytes_left == 0) {
            break;
        }
    }

#ifdef __Fuchsia__
    read_time_ += zx_ticks_get() - start;
#endif

    if (r < 0) {
        return static_cast<zx_status_t>(r);
    }

    *actual = total_size;
    return ZX_OK;
}

zx_status_t SparseReader::WriteDecompressed(fbl::unique_fd outfd) {
    if (!compressed_) {
        fprintf(stderr, "BlockReader: File is not compressed\n");
        return ZX_ERR_INVALID_ARGS;
    }

    // Update metadata and write to new file.
    fvm::sparse_image_t* image = Image();
    image->flags &= ~fvm::kSparseFlagLz4;

    if (write(outfd.get(), metadata_.get(), image->header_length)
        != static_cast<ssize_t>(image->header_length)) {
        fprintf(stderr, "BlockReader: could not write header to out file\n");
        return -1;
    }

    // Read/write decompressed data in LZ4_MAX_BLOCK_SIZE chunks.
    while (true) {
        zx_status_t status;
        uint8_t data[LZ4_MAX_BLOCK_SIZE];
        size_t length;
        if ((status = ReadData(data, LZ4_MAX_BLOCK_SIZE, &length)) != ZX_OK) {
            if (status == ZX_ERR_OUT_OF_RANGE) {
                return ZX_OK;
            }

            return status;
        }

        if (write(outfd.get(), data, length) != static_cast<ssize_t>(length)) {
            fprintf(stderr, "BlockReader: failed to write to output\n");
            return ZX_ERR_IO;
        }
    }
}

void SparseReader::PrintStats() const {
    printf("Reading FVM from compressed file: %s\n", compressed_ ? "true" : "false");
    printf("Remaining bytes read into compression buffer:    %lu\n", in_buf_.size);
    printf("Remaining bytes written to decompression buffer: %lu\n", out_buf_.size);
#ifdef __Fuchsia__
    printf("Time reading bytes from sparse FVM file:   %lu (%lu s)\n", read_time_,
           read_time_ / zx_ticks_per_second());
    printf("Time reading bytes AND decompressing them: %lu (%lu s)\n", total_time_,
           total_time_ / zx_ticks_per_second());
#endif
}

zx_status_t decompress_sparse(const char* infile, const char* outfile) {
    fbl::unique_fd infd, outfd, testfd;

    infd.reset(open(infile, O_RDONLY));
    if (!infd) {
        fprintf(stderr, "could not open %s: %s\n", infile, strerror(errno));
        return ZX_ERR_IO;
    }

    outfd.reset(open(outfile, O_WRONLY | O_CREAT | O_EXCL, 0644));
    if (!outfd) {
        fprintf(stderr, "could not open %s: %s\n", outfile, strerror(errno));
        return ZX_ERR_IO;
    }

    zx_status_t status;
    fbl::unique_ptr<SparseReader> reader;
    if ((status = SparseReader::Create(fbl::move(infd), &reader))) {
        return status;
    }

    return reader->WriteDecompressed(fbl::move(outfd));
}

} // namespace fvm
